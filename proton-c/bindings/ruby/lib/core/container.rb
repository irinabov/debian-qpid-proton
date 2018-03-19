# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.


require 'thread'
require 'set'
require_relative 'listener'

module Qpid::Proton
  # An AMQP container manages a set of {Listener}s and {Connection}s which
  # contain {#Sender} and {#Receiver} links to transfer messages.  Usually, each
  # AMQP client or server process has a single container for all of its
  # connections and links.
  #
  # One or more threads can call {#run}, events generated by all the listeners and
  # connections will be dispatched in the {#run} threads.
  class Container
    private

    # Container driver applies options and adds container context to events
    class ConnectionTask < Qpid::Proton::HandlerDriver
      def initialize container, io, opts, server=false
        super io, opts[:handler]
        transport.set_server if server
        transport.apply opts
        connection.apply opts
      end
    end

    class ListenTask < Listener

      def initialize(io, handler, container)
        super
        @closing = @closed = nil
        env = ENV['PN_TRACE_EVT']
        if env && ["true", "1", "yes", "on"].include?(env.downcase)
          @log_prefix = "[0x#{object_id.to_s(16)}](PN_LISTENER_"
        else
          @log_prefix = nil
        end
        dispatch(:on_open);
      end

      def process
        return if @closed
        unless @closing
          begin
            return @io.accept, dispatch(:on_accept)
          rescue IO::WaitReadable, Errno::EINTR
          rescue IOError, SystemCallError => e
            close e
          end
        end
      ensure
        if @closing
          @io.close rescue nil
          @closed = true
          dispatch(:on_error, @condition) if @condition
          dispatch(:on_close)
        end
      end

      def can_read?() !finished?; end
      def can_write?() false; end
      def finished?() @closed; end

      def dispatch(method, *args)
        # TODO aconway 2017-11-27: better logging
        STDERR.puts "#{@log_prefix}#{([method[3..-1].upcase]+args).join ', '})" if @log_prefix
        @handler.__send__(method, self, *args) if @handler && @handler.respond_to?(method)
      end
    end

    public

    # Error raised if the container is used after {#stop} has been called.
    class StoppedError < RuntimeError
      def initialize(*args) super("container has been stopped"); end
    end

    # Create a new Container
    # @overload initialize(id=nil)
    #   @param id [String,Symbol] A unique ID for this container, use random UUID if nil.
    #
    # @overload initialize(handler=nil, id=nil)
    #  @param id [String,Symbol] A unique ID for this container, use random UUID if nil.
    #  @param handler [MessagingHandler] Optional default handler for connections
    #   that do not have their own handler (see {#connect} and {#listen})
    #
    #   *Note*: For multi-threaded code, it is recommended to use a separate
    #   handler instance for each connection, as a shared handler may be called
    #   concurrently.
    #
    def initialize(*args)
      case args.size
      when 2 then @handler, @id = args
      when 1 then
        @id = String.try_convert(args[0]) || (args[0].to_s if args[0].is_a? Symbol)
        @handler = args[0] unless @id
      else raise ArgumentError, "wrong number of arguments (given #{args.size}, expected 0..2"
      end
      # Use an empty messaging adapter to give default behaviour if there's no global handler.
      @adapter = Handler::Adapter.adapt(@handler) || Handler::MessagingAdapter.new(nil)
      @id = (@id || SecureRandom.uuid).freeze

      # Implementation note:
      #
      # - #run threads take work from @work
      # - Each driver and the Container itself is processed by at most one #run thread at a time
      # - The Container thread does IO.select
      # - nil on the @work queue makes a #run thread exit

      @work = Queue.new
      @work << :start
      @work << self             # Issue start and start start selecting
      @wake = IO.pipe           # Wakes #run thread in IO.select
      @auto_stop = true         # Stop when @active drops to 0

      # Following instance variables protected by lock
      @lock = Mutex.new
      @active = 0               # All active tasks, in @selectable, @work or being processed
      @selectable = Set.new     # Tasks ready to block in IO.select
      @running = 0              # Count of #run threads
      @stopped = false          # #stop called
      @stop_err = nil           # Optional error to pass to tasks, from #stop
    end

    # @return [MessagingHandler] The container-wide handler
    attr_reader :handler

    # @return [String] unique identifier for this container
    attr_reader :id

    # Auto-stop flag.
    #
    # True (the default) means that the container will stop automatically, as if {#stop}
    # had been called, when the last listener or connection closes.
    #
    # False means {#run} will not return unless {#stop} is called.
    #
    # @return [Bool] auto-stop state
    attr_accessor :auto_stop

    # True if the container has been stopped and can no longer be used.
    # @return [Bool] stopped state
    attr_accessor :stopped

    # Number of threads in {#run}
    # @return [Bool] {#run} thread count
    def running; @lock.synchronize { @running }; end

    # Open an AMQP connection.
    #
    # @param url [String, URI] Open a {TCPSocket} to url.host, url.port.
    # url.scheme must be "amqp" or "amqps", url.scheme.nil? is treated as "amqp"
    # url.user, url.password are used as defaults if opts[:user], opts[:password] are nil
    # @option (see Connection#open)
    # @return [Connection] The new AMQP connection
    def connect(url, opts=nil)
      not_stopped
      url = Qpid::Proton::uri url
      opts ||= {}
      if url.user ||  url.password
        opts[:user] ||= url.user
        opts[:password] ||= url.password
      end
      opts[:ssl_domain] ||= SSLDomain.new(SSLDomain::MODE_CLIENT) if url.scheme == "amqps"
      connect_io(TCPSocket.new(url.host, url.port), opts)
    end

    # Open an AMQP protocol connection on an existing {IO} object
    # @param io [IO] An existing {IO} object, e.g. a {TCPSocket}
    # @option (see Connection#open)
    def connect_io(io, opts=nil)
      not_stopped
      cd = connection_driver(io, opts)
      cd.connection.open()
      add(cd)
      cd.connection
    end

    # Listen for incoming AMQP connections
    #
    # @param url [String,URI] Listen on host:port of the AMQP URL
    # @param handler [Listener::Handler] A {Listener::Handler} object that will be called
    # with events for this listener and can generate a new set of options for each one.
    # @return [Listener] The AMQP listener.
    #
    def listen(url, handler=Listener::Handler.new)
      not_stopped
      url = Qpid::Proton::uri url
      # TODO aconway 2017-11-01: amqps, SSL
      listen_io(TCPServer.new(url.host, url.port), handler)
    end

    # Listen for incoming AMQP connections on an existing server socket.
    # @param io A server socket, for example a {TCPServer}
    # @param handler [Listener::Handler] Handler for events from this listener
    #
    def listen_io(io, handler=Listener::Handler.new)
      not_stopped
      l = ListenTask.new(io, handler, self)
      add(l)
      l
    end

    # Run the container: wait for IO activity, dispatch events to handlers.
    #
    # More than one thread can call {#run} concurrently, the container will use
    # all the {#run} threads as a thread pool. Calls to
    # {Handler::MessagingHandler} methods are serialized for each connection or
    # listener, even if the container has multiple threads.
    #
    def run
      @lock.synchronize do
        @running += 1        # Note: ensure clause below will decrement @running
        raise StoppedError if @stopped
      end
      while task = @work.pop
        case task

        when :start
          @adapter.on_container_start(self) if @adapter.respond_to? :on_container_start

        when Container
          r, w = [@wake[0]], []
          @lock.synchronize do
            @selectable.each do |s|
              r << s if s.send :can_read?
              w << s if s.send :can_write?
            end
          end
          r, w = IO.select(r, w)
          selected = Set.new(r).merge(w)
          drain_wake if selected.delete?(@wake[0])
          stop_select = nil
          @lock.synchronize do
            if stop_select = @stopped # close everything
              selected += @selectable
              selected.each { |s| s.close @stop_err }
              @wake.each { |fd| fd.close() }
            end
            @selectable -= selected # Remove selected tasks
          end
          selected.each { |s| @work << s } # Queue up tasks needing #process
          @work << self unless stop_select

        when ConnectionTask then
          task.process
          rearm task

        when ListenTask then
          io, opts = task.process
          add(connection_driver(io, opts, true)) if io
          rearm task
        end
        # TODO aconway 2017-10-26: scheduled tasks, heartbeats
      end
    ensure
      @lock.synchronize do
        if (@running -= 1) > 0
          work_wake nil         # Signal the next thread
        else
          @adapter.on_container_stop(self) if @adapter.respond_to? :on_container_stop
        end
      end
    end

    # Stop the container.
    #
    # Close all listeners and abort all connections without doing AMQP protocol close.
    #
    # {#stop} returns immediately, calls to {#run} will return when all activity
    # is finished.
    #
    # The container can no longer be used, using a stopped container raises
    # {StoppedError} on attempting.  Create a new container if you want to
    # resume activity.
    #
    # @param error [Condition] Optional transport/listener error condition
    #
    def stop(error=nil)
      @lock.synchronize do
        raise StoppedError if @stopped
        @stopped = true
        @stop_err = Condition.convert(error)
        check_stop_lh
        # NOTE: @stopped =>
        # - no new run threads can join
        # - no more select calls after next wakeup
        # - once @active == 0, all threads will be stopped with nil
      end
      wake
    end

    protected

    def wake; @wake[1].write_nonblock('x') rescue nil; end

    # Normally if we add work we need to set a wakeup to ensure a single #run
    # thread doesn't get stuck in select while there is other work on the queue.
    def work_wake(task) @work << task; wake; end

    def drain_wake
      begin
        @wake[0].read_nonblock(256) while true
      rescue Errno::EWOULDBLOCK, Errno::EAGAIN, Errno::EINTR
      end
    end

    def connection_driver(io, opts=nil, server=false)
      opts ||= {}
      opts[:container] = self
      opts[:handler] ||= @adapter
      ConnectionTask.new(self, io, opts, server)
    end

    # All new tasks are added here
    def add task
      @lock.synchronize do
        @active += 1
        task.close @stop_err if @stopped
      end
      work_wake task
    end

    def rearm task
      @lock.synchronize do
        if task.finished?
          @active -= 1
          check_stop_lh
        elsif @stopped
          task.close @stop_err
          work_wake task
        else
          @selectable << task
        end
      end
      wake
    end

    def check_stop_lh
      if @active.zero? && (@auto_stop || @stopped)
        @stopped = true
        work_wake nil          # Signal threads to stop
        true
      end
    end

    def not_stopped; raise StoppedError if @lock.synchronize { @stopped }; end

  end
end
