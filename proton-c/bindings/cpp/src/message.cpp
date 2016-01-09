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

#include "proton/message.hpp"
#include "proton/error.hpp"
#include "proton/link.hpp"
#include "proton/delivery.hpp"
#include "proton/message.h"
#include "proton/sender.hpp"
#include "proton/receiver.hpp"
#include "proton/delivery.h"
#include "msg.hpp"
#include "proton_bits.hpp"

#include <cstring>
#include <assert.h>

namespace proton {

message::message() : message_(::pn_message()) {}

message::message(const message &m) : message_(::pn_message()) { *this = m; }

#if PN_HAS_CPP11
message::message(message &&m) : message_(::pn_message()) { swap(m); }
#endif

message::~message() { ::pn_message_free(message_); }

void message::swap(message& m) { std::swap(message_, m.message_); }

message& message::operator=(const message& m) {
    // TODO aconway 2015-08-10: more efficient pn_message_copy function
    std::string data;
    m.encode(data);
    decode(data);
    return *this;
}

void message::clear() { pn_message_clear(message_); }

namespace {
void check(int err) {
    if (err) throw error(error_str(err));
}
} // namespace

void message::id(const data& id) { *data::cast(pn_message_id(message_)) = id; }
const data& message::id() const { return *data::cast(pn_message_id(message_)); }
data& message::id() { return *data::cast(pn_message_id(message_)); }

void message::user_id(const std::string &id) {
    check(pn_message_set_user_id(message_, pn_bytes(id)));
}

std::string message::user_id() const {
    return str(pn_message_get_user_id(message_));
}

void message::address(const std::string &addr) {
    check(pn_message_set_address(message_, addr.c_str()));
}

std::string message::address() const {
    const char* addr = pn_message_get_address(message_);
    return addr ? std::string(addr) : std::string();
}

void message::subject(const std::string &s) {
    check(pn_message_set_subject(message_, s.c_str()));
}

std::string message::subject() const {
    const char* s = pn_message_get_subject(message_);
    return s ? std::string(s) : std::string();
}

void message::reply_to(const std::string &s) {
    check(pn_message_set_reply_to(message_, s.c_str()));
}

std::string message::reply_to() const {
    const char* s = pn_message_get_reply_to(message_);
    return s ? std::string(s) : std::string();
}

void message::correlation_id(const data& id) {
    *data::cast(pn_message_correlation_id(message_)) = id;
}

const data& message::correlation_id() const {
    return *data::cast(pn_message_correlation_id(message_));
}

data& message::correlation_id() {
    return *data::cast(pn_message_correlation_id(message_));
}

void message::content_type(const std::string &s) {
    check(pn_message_set_content_type(message_, s.c_str()));
}

std::string message::content_type() const {
    const char* s = pn_message_get_content_type(message_);
    return s ? std::string(s) : std::string();
}

void message::content_encoding(const std::string &s) {
    check(pn_message_set_content_encoding(message_, s.c_str()));
}

std::string message::content_encoding() const {
    const char* s = pn_message_get_content_encoding(message_);
    return s ? std::string(s) : std::string();
}

void message::expiry_time(amqp_timestamp t) {
    pn_message_set_expiry_time(message_, t.milliseconds);
}
amqp_timestamp message::expiry_time() const {
    return amqp_timestamp(pn_message_get_expiry_time(message_));
}

void message::creation_time(amqp_timestamp t) {
    pn_message_set_creation_time(message_, t);
}
amqp_timestamp message::creation_time() const {
    return pn_message_get_creation_time(message_);
}

void message::group_id(const std::string &s) {
    check(pn_message_set_group_id(message_, s.c_str()));
}

std::string message::group_id() const {
    const char* s = pn_message_get_group_id(message_);
    return s ? std::string(s) : std::string();
}

void message::reply_to_group_id(const std::string &s) {
    check(pn_message_set_reply_to_group_id(message_, s.c_str()));
}

std::string message::reply_to_group_id() const {
    const char* s = pn_message_get_reply_to_group_id(message_);
    return s ? std::string(s) : std::string();
}

void message::body(const data& v) { body() = v; }

const data& message::body() const {
    return *data::cast(pn_message_body(message_));
}

data& message::body() {
    return *data::cast(pn_message_body(message_));
}

void message::encode(std::string &s) const {
    size_t sz = s.capacity();
    if (sz < 512) sz = 512;
    while (true) {
        s.resize(sz);
        int err = pn_message_encode(message_, (char *) s.data(), &sz);
        if (err) {
            if (err != PN_OVERFLOW)
                check(err);
        } else {
            s.resize(sz);
            return;
        }
        sz *= 2;
    }
}

std::string message::encode() const {
    std::string data;
    encode(data);
    return data;
}

void message::decode(const std::string &s) {
    check(pn_message_decode(message_, s.data(), s.size()));
}

void message::decode(proton::link &link, proton::delivery &delivery) {
    std::string buf;
    buf.resize(pn_delivery_pending(pn_cast(&delivery)));
    ssize_t n = pn_link_recv(pn_cast(&link), (char *) buf.data(), buf.size());
    if (n != (ssize_t) buf.size()) throw error(MSG("link read failure"));
    clear();
    decode(buf);
    pn_link_advance(pn_cast(&link));
}

}



