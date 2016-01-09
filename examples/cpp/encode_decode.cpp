/*
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
 */

#include <proton/value.hpp>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>

using namespace std;

// Examples of how to use the encoder and decoder to create and examine AMQP values.
//

// Print is defined at the end as an example of how to query and extract complex
// values in terms of their simple components.
void print(proton::data&);

// Inserting and extracting simple C++ values.
void simple_insert_extract() {
    cout << endl << "== Simple values: int, string, bool" << endl;
    proton::value dv;
    dv.encoder() << 42 << "foo" << true;
    print(dv);
    int i;
    string s;
    bool b;
    dv.decoder().rewind();
    dv.decoder() >> i >> s >> b;
    cout << "Extracted: " << i << ", " << s << ", " << b << endl;
    // Encode and decode as AMQP
    string amqp_data = dv.encoder().encode();
    cout << "Encoded as AMQP in " << amqp_data.size() << " bytes" << endl;
    proton::value  dt2;
    dt2.decoder().decode(amqp_data);
    dt2.decoder() >> i >> s >> b;
    cout << "Decoded: " << i << ", " << s << ", " << b << endl;
}

// Inserting values as a specific AMQP type
void simple_insert_extract_exact_type() {
    proton::value dv;
    cout << endl << "== Specific AMQP types: byte, long, symbol" << endl;
    dv.encoder() << proton::amqp_byte('x') << proton::amqp_long(123456789123456789) << proton::amqp_symbol("bar");
    print(dv);
    dv.decoder().rewind();
    // Check that we encoded the correct types, but note that decoding will
    // still convert to standard C++ types, in particular any AMQP integer type
    // can be converted to a long-enough C++ integer type..
    int64_t i1, i2;
    string s;
    dv.decoder() >> i1 >> i2 >> s;
    cout << "Extracted (with conversion) " << i1 << ", " << i2 << ", " << s << endl;

    // Now use the as() function to fail unless we extract the exact AMQP type expected.
    dv.decoder().rewind(); // amqp_byte(1) << amqp_long(2) << amqp_symbol("bar");
    proton::amqp_long l;
    // Fails, extracting amqp_byte as amqp_long
    try { dv.decoder() >> proton::as<proton::LONG>(l); throw logic_error("expected error"); } catch (proton::decode_error) {}
    proton::amqp_byte b;
    dv.decoder() >> proton::as<proton::BYTE>(b) >> proton::as<proton::LONG>(l); // OK, extract amqp_byte as amqp_byte, amqp_long as amqp_long.
    string str;
    // Fails, extracting amqp_symbol as amqp_string.
    try { dv.decoder() >> proton::as<proton::STRING>(str); throw logic_error("expected error"); } catch (proton::decode_error) {}
    dv.decoder() >> proton::as<proton::SYMBOL>(str);       // OK, extract amqp_symbol as amqp_symbol
    cout << "Extracted (exact) " << b << ", " << l << ", " << str << endl;
}

// Some helper templates to print map and vector results.
namespace std {
template<class T, class U> ostream& operator<<(ostream& o, const pair<T,U>& p) {
    return o << p.first << ":" << p.second;
}
template<class T> ostream& operator<<(ostream& o, const vector<T>& v) {
    o << "[ ";
    ostream_iterator<T> oi(o, " ");
    copy(v.begin(), v.end(), oi);
    return o << "]";
}
template<class K, class T> ostream& operator<<(ostream& o, const map<K, T>& m) {
    o << "{ ";
    ostream_iterator<pair<K,T> > oi(o, " ");
    copy(m.begin(), m.end(), oi);
    return o << "}";
}
}

// Insert/extract C++ containers.
void insert_extract_containers() {
    cout << endl << "== Array, list and map." << endl;

    vector<int> a;
    a.push_back(1);
    a.push_back(2);
    a.push_back(3);
    vector<int> l;
    l.push_back(4);
    l.push_back(5);
    map<string, int> m;
    m["one"] = 1;
    m["two"] = 2;

    proton::value dv;
    dv.encoder() << proton::as<proton::ARRAY>(a) << proton::as<proton::LIST>(l) << proton::as<proton::MAP>(m);
    print(dv);

    vector<int> a1, l1;
    map<string, int> m1;
    dv.decoder().rewind();
    dv.decoder() >> proton::as<proton::ARRAY>(a1) >> proton::as<proton::LIST>(l1) >> proton::as<proton::MAP>(m1);
    cout << "Extracted: " << a1 << ", " << l1 << ", " << m1 << endl;
}

// Containers with mixed types, use value to represent arbitrary AMQP types.
void mixed_containers() {
    cout << endl << "== List and map of mixed type values." << endl;
    vector<proton::value> l;
    l.push_back(proton::value(42));
    l.push_back(proton::value(proton::amqp_string("foo")));
    map<proton::value, proton::value> m;
    m[proton::value("five")] = proton::value(5);
    m[proton::value(4)] = proton::value("four");
    proton::value dv;
    dv.encoder() << proton::as<proton::LIST>(l) << proton::as<proton::MAP>(m);
    print(dv);

    vector<proton::value> l1;
    map<proton::value, proton::value> m1;
    dv.decoder().rewind();
    dv.decoder() >> proton::as<proton::LIST>(l1) >> proton::as<proton::MAP>(m1);
    cout << "Extracted: " << l1 << ", " << m1 << endl;
}

// Insert using stream operators (see print_next for example of extracting with stream ops.)
void insert_extract_stream_operators() {
    cout << endl << "== Insert with stream operators." << endl;
    proton::value dv;
    // Note: array elements must be encoded with the exact type, they are not
    // automaticlly converted. Mismatched types for array elements will not
    // be detected until dv.encode() is called.
    dv.encoder() << proton::start::array(proton::INT) << proton::amqp_int(1) << proton::amqp_int(2) << proton::amqp_int(3) << proton::finish();
    print(dv);

    dv.clear();
    dv.encoder() << proton::start::list() << proton::amqp_int(42) << false << proton::amqp_symbol("x") << proton::finish();
    print(dv);

    dv.clear();
    dv.encoder() << proton::start::map() << "k1" << proton::amqp_int(42) << proton::amqp_symbol("k2") << false << proton::finish();
    print(dv);
}

int main(int, char**) {
    try {
        simple_insert_extract();
        simple_insert_extract_exact_type();
        insert_extract_containers();
        mixed_containers();
        insert_extract_stream_operators();
        return 0;
    } catch (const exception& e) {
        cerr << endl << "error: " << e.what() << endl;
    }
    return 1;
}

// print_next prints the next value from values by recursively descending into complex values.
//
// NOTE this is for example puroses only: There is a built in ostream operator<< for values.
//
//
void print_next(proton::data& dv) {
    proton::type_id type = dv.type();
    proton::start s;
    switch (type) {
      case proton::ARRAY: {
          dv.decoder() >> s;
          cout << "array<" << s.element;
          if (s.is_described) {
              cout  << ", descriptor=";
              print_next(dv);
          }
          cout << ">[";
          for (size_t i = 0; i < s.size; ++i) {
              if (i) cout << ", ";
              print_next(dv);
          }
          cout << "]";
          dv.decoder() >> proton::finish();
          break;
      }
      case proton::LIST: {
          dv.decoder() >> s;
          cout << "list[";
          for (size_t i = 0; i < s.size; ++i) {
              if (i) cout << ", ";
              print_next(dv);
          }
          cout << "]";
          dv.decoder() >> proton::finish();
          break;
      }
      case proton::MAP: {
          dv.decoder() >> s;
          cout << "map{";
          for (size_t i = 0; i < s.size/2; ++i) {
              if (i) cout << ", ";
              print_next(dv);
              cout << ":";        // key:value
              print_next(dv);
          }
          cout << "}";
          dv.decoder() >> proton::finish();
          break;
      }
      case proton::DESCRIBED: {
          dv.decoder() >> s;
          cout << "described(";
          print_next(dv);      // Descriptor
          print_next(dv);      // value
          dv.decoder() >> proton::finish();
          break;
      }
      default:
        // A simple type. We could continue the switch for all AMQP types but
        // instead we us the `value` type which can hold and print any AMQP
        // value.
        proton::value v;
        dv.decoder() >> v;
        cout << type << "(" << v << ")";
    }
}

// Print all the values with print_next
void print(proton::data& dv) {
    dv.decoder().rewind();
    cout << "Values: ";
    while (dv.decoder().more()) {
        print_next(dv);
        if (dv.decoder().more()) cout << ", ";
    }
    cout << endl;
}
