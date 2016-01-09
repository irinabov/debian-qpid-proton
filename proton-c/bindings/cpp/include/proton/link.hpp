#ifndef PROTON_CPP_LINK_H
#define PROTON_CPP_LINK_H

/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
#include "proton/endpoint.hpp"
#include "proton/export.hpp"
#include "proton/message.hpp"
#include "proton/terminus.hpp"
#include "proton/types.h"
#include "proton/facade.hpp"

#include <string>

namespace proton {

class sender;
class receiver;

/** Messages are transferred across a link. Base class for sender, receiver. */
class link : public counted_facade<pn_link_t, link, endpoint>
{
  public:
    /** Locally open the link, not complete till messaging_handler::on_link_opened or
     * proton_handler::link_remote_open
     */
    PN_CPP_EXTERN void open();

    /** Locally close the link, not complete till messaging_handler::on_link_closed or
     * proton_handler::link_remote_close
     */
    PN_CPP_EXTERN void close();

    /** Return sender if this link is a sender, 0 if not. */
    PN_CPP_EXTERN class sender* sender();
    PN_CPP_EXTERN const class sender* sender() const;

    /** Return receiver if this link is a receiver, 0 if not. */
    PN_CPP_EXTERN class receiver* receiver();
    PN_CPP_EXTERN const class receiver* receiver() const;

    /** Credit available on the link */
    PN_CPP_EXTERN int credit() const;

    /** Local source of the link. */
    PN_CPP_EXTERN terminus& source() const;
    /** Local target of the link. */
    PN_CPP_EXTERN terminus& target() const;
    /** Remote source of the link. */
    PN_CPP_EXTERN terminus& remote_source() const;
    /** Remote target of the link. */
    PN_CPP_EXTERN terminus& remote_target() const;

    /** Link name */
    PN_CPP_EXTERN std::string name() const;

    /** Connection that owns this link */
    PN_CPP_EXTERN class connection &connection() const;

    /** Session that owns this link */
    PN_CPP_EXTERN class session &session() const;

    /** Set a custom handler for this link. */
    PN_CPP_EXTERN void handler(class handler &);

    /** Unset any custom handler */
    PN_CPP_EXTERN void detach_handler();

    /** Get the endpoint state */
    PN_CPP_EXTERN endpoint::state state() const;
};

}

#include "proton/sender.hpp"
#include "proton/receiver.hpp"

#endif  /*!PROTON_CPP_LINK_H*/
