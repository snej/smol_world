//
// Test_Sparse.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "sparse_hash.hh"
#include "sparse_hash_io.hh"
#include "HashTable.hh"
#include "catch.hpp"
#include <iostream>

using namespace std;
using namespace snej::smol;


TEST_CASE("Sparse Bucket", "[sparse]") {
    sparse_bucket<string, 128> bucket;

    CHECK(bucket.count() == 0);
    CHECK(bucket.empty());
    CHECK(!bucket.contains(0));
    CHECK(!bucket.contains(63));
    CHECK(bucket.get(31) == "");

    string* item = bucket.insert(17, "seventeen");
    CHECK(item != nullptr);
    CHECK(*item == "seventeen");
    CHECK(bucket.count() == 1);
    CHECK(!bucket.empty());
    CHECK(bucket.contains(17));
    CHECK(!bucket.contains(16));
    CHECK(bucket.get(17) == "seventeen");

    CHECK(!bucket.insert(17, "seventeen"));
    CHECK(bucket.put(17, "17"));
    CHECK(bucket.get(17) == "17");

    CHECK(bucket.insert(8, "eight"));
    CHECK(bucket.insert(61, "sixty-one"));
    CHECK(bucket.insert(0, "zero"));
    CHECK(bucket.insert(63, "sixty-three"));

    CHECK(bucket.count() == 5);
    CHECK(bucket.get(17) == "17");
    CHECK(bucket.get(8) == "eight");
    CHECK(bucket.get(61) == "sixty-one");

    CHECK(bucket.insert(123, "onetwothree"));
    CHECK(bucket.count() == 6);
    CHECK(bucket.contains(123));
    CHECK(!bucket.contains(122));
    CHECK(bucket.get(123) == "onetwothree");

    CHECK(bucket.insert(127, "onetwoseven"));
    CHECK(bucket.count() == 7);
    CHECK(bucket.contains(127));
    CHECK(bucket.contains(123));
    CHECK(bucket.get(127) == "onetwoseven");

    for (unsigned i = 0; i < 128; ++i)
        bucket.put(i, to_string(i));
    CHECK(bucket.count() == 128);
    for (unsigned i = 0; i < 128; ++i)
        CHECK(bucket.get(i) == to_string(i));
}


TEST_CASE("Sparse Array", "[sparse]") {
    sparse_array<string, 128> array(256);
    CHECK(array.size() == 256);
    CHECK(array.count() == 0);
    for (unsigned i = 0; i < 256; i++)
        CHECK(!array.contains(i));

    CHECK(array.insert(17, "seventeen"));
    CHECK(array.count() == 1);
    CHECK(!array.empty());
    CHECK(array.contains(17));
    CHECK(!array.contains(16));
    CHECK(array.get(17) == "seventeen");

    CHECK(array.insert(250, "twofifty"));
    CHECK(array.count() == 2);
    CHECK(!array.empty());
    CHECK(array.contains(250));
    CHECK(!array.contains(200));
    CHECK(array.get(250) == "twofifty");

    CHECK(array.insert(128, "onetwentyeight"));
    CHECK(array.count() == 3);
    CHECK(array.contains(128));
    CHECK(!array.contains(127));
    CHECK(array.get(128) == "onetwentyeight");

    for (unsigned i = 0; i < 256; i++)
        CHECK(array.contains(i) == (i == 17 || i == 250 || i == 128));
}


struct strHash {
    uint32_t operator() (string_view const& str) { return HashTable::computeHash(str); };
};


TEST_CASE("Sparse Hash", "[sparse]") {
    sparse_hash_table<string_view,string,strHash> hash(150);
    CHECK(hash.capacity() >= 150);
    CHECK(hash.count() == 0);

    CHECK(hash.get("foo") == nullptr);

    string* foo = hash.put("foo");
    REQUIRE(foo);
    CHECK(*foo == "foo");
    CHECK(hash.put("foo") == foo);

    string* bar = hash.put("bar");
    REQUIRE(bar);
    CHECK(*bar == "bar");

    hash.dump(cout);

    for (unsigned i = 0; i < 20; ++i)
        hash.put(to_string(i));
    CHECK(hash.count() == 22);
    for (unsigned i = 0; i < 20; ++i)
        CHECK(hash.get(to_string(i)));

    cout << endl;
    hash.dump(cout);

    for (unsigned i = 20; i < 128; ++i)
        hash.put(to_string(i));
    CHECK(hash.count() == 130);
    for (unsigned i = 0; i < 128; ++i)
        CHECK(hash.get(to_string(i)));

    cout << endl;
    hash.dump(cout);
}
