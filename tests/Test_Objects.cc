//
// ObjectTests.cc
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

#include "smol_world.hh"
#include "catch.hpp"
#include <iostream>

using namespace std;


TEST_CASE("Strings", "[object]") {
    constexpr string_view kString = "Hello, smol world!";
    Heap heap(1000);
    UsingHeap u(heap);

    for (int len = 0; len <= kString.size(); ++len) {
        INFO("len is " << len);
        string_view str = kString.substr(0, len);
        auto obj = String::create(str, heap);
        REQUIRE(obj);
        REQUIRE(obj.type() == Type::String);
        REQUIRE(obj.is<String>());
        Val val = obj.asVal(heap);
        CHECK(val.type(heap) == Type::String);
        CHECK(val.as<String>(heap) == obj);

        CHECK(obj.capacity() == len);
        CHECK(obj.count() == len);
        CHECK(obj.empty() == (len == 0));
        CHECK(obj.get() == str);
        CHECK(string(obj.begin(), obj.end()) == str);

    }
}


TEST_CASE("Blobs", "[object]") {
    constexpr array<uint8_t,10> kBlob = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    Heap heap(1000);
    UsingHeap u(heap);

    for (int len = 0; len <= kBlob.size(); ++len) {
        INFO("len is " << len);
        auto obj = Blob::create(kBlob.data(), len, heap);
        REQUIRE(obj);
        REQUIRE(obj.type() == Type::Blob);
        REQUIRE(obj.is<Blob>());
        Val val = obj.asVal(heap);
        CHECK(val.as<Blob>(heap) == obj);

        CHECK(obj.capacity() == len);
        CHECK(obj.count() == len);
        CHECK(obj.empty() == (len == 0));
        CHECK(memcmp(kBlob.begin(), obj.begin(), len) == 0);
    }
}


TEST_CASE("Arrays", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);

    String strs[10];
    for (int i = 0; i < 10; ++i)
        strs[i] = String::create(std::to_string(i), heap);

    for (int len = 0; len <= 10; ++len) {
        INFO("len is " << len);
        auto obj = Array::create(len, heap);
        REQUIRE(obj);
        REQUIRE(obj.type() == Type::Array);
        REQUIRE(obj.is<Array>());
        Val val = obj.asVal(heap);
        CHECK(val.as<Array>(heap) == obj);

        CHECK(obj.capacity() == len);
        CHECK(obj.count() == len);
        CHECK(obj.empty() == (len == 0));

        for (int i = 0; i < len; ++i)
            obj[i] = strs[i].asVal(heap);
        for (int i = 0; i < len; ++i)
            CHECK(obj[i].as<String>(heap) == strs[i]);
    }
}


template <typename T>
static void shuffle(T* begin, T* end) {
    auto len = end - begin;
    for (auto i = len - 1; i > 0; --i)
        swap(begin[i], begin[random() % (i+1)]);
}


TEST_CASE("Dicts", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);
    srandomdev();

    String strs[11];
    for (int i = 0; i < 11; ++i)
        strs[i] = String::create(std::to_string(i), heap);
    shuffle(strs+0, strs+11);

    for (int len = 0; len <= 10; ++len) {
        //cerr << "len is " << len << endl;
        INFO("len is " << len);
        auto obj = Dict::create(len, heap);
        REQUIRE(obj);
        REQUIRE(obj.type() == Type::Dict);
        REQUIRE(obj.is<Dict>());
        Val val = obj.asVal(heap);
        CHECK(val.as<Dict>(heap) == obj);

        CHECK(obj.capacity() == len);
        CHECK(obj.empty());

        for (int i = 0; i <= len; ++i) {
            INFO("i = " << i);
            Val key = strs[i].asVal(heap);
            CHECK(obj.count() == i);
            CHECK(obj.full() == (i == len));
            CHECK(!obj.contains(key));
            CHECK(!obj.replace(key, -1));
            if (i < len) {
                CHECK(obj.set(key, i));
                CHECK(obj.get(key) == i);
                CHECK(obj.contains(key));
                CHECK(!obj.empty());
                CHECK(!obj.insert(key, -1));
                CHECK(obj.replace(key, -i));

                for (int j = 0; j < 10; ++j)
                    CHECK(obj.get(strs[j].asVal(heap)) == ((j <= i) ? Val(-j) : nullval));

            } else {
                CHECK(!obj.set(key, i));
                CHECK(!obj.insert(key, -1));
                CHECK(!obj.contains(key));
            }
        }

        shuffle(strs+0, strs+len);
        for (int i = 0; i < len; ++i) {
            INFO("i = " << i);
            Val key = strs[i].asVal(heap);
            CHECK(obj.count() == len-i);
            CHECK(obj.full() == (i == 0));
            CHECK(obj.contains(key));
            CHECK(obj.remove(key));
            CHECK(!obj.contains(key));
            CHECK(!obj.remove(key));
            CHECK(obj.empty() == (i == len-1));
        }
    }
}


TEST_CASE("Symbols", "[object]") {
    Heap heap(10000);
    SymbolTable& table = heap.symbolTable();
    CHECK(table.count() == 0);

    CHECK(table.find("foo") == nullptr);

    auto foo = table.create("foo");
    REQUIRE(foo);
    CHECK(foo.get() == "foo");
    CHECK(table.find("foo") == foo);

    auto bar = table.create("bar");
    REQUIRE(bar);
    CHECK(bar.get() == "bar");
    CHECK(table.find("bar") == bar);
    CHECK(table.count() == 2);

    constexpr size_t NumSymbols = 100;
    Symbol syms[NumSymbols];
    for (size_t i = 0; i < NumSymbols; ++i) {
        string name = "Symbol #" + std::to_string(i * i);
        cerr << "Creating #" << i << ": " << name << endl;
        CHECK(table.find(name) == nullptr);
        auto sym = table.create(name);
        syms[i] = sym;
        REQUIRE(sym);
        CHECK(sym.get() == name);
        CHECK(table.find(name) == sym);
        CHECK(table.count() == 3 + i);
    }
    for (size_t i = 0; i < NumSymbols; ++i) {
        string name = "Symbol #" + std::to_string(i * i);
        CHECK(table.find(name) == syms[i]);
    }

    size_t i = 0;
    table.visit([&](Symbol sym, uint32_t) {
        ++i;
        return true;
    });
    cerr << endl;
    CHECK(i == 2 + NumSymbols);

    // Open a heap from the current heap and check it has everything:
    Heap heap2 = Heap::existing((void*)heap.base(), heap.used(), heap.capacity());
    SymbolTable& table2 = heap2.symbolTable();
    auto bar2 = table2.find("bar");
    CHECK(bar2);
    CHECK(bar2.get() == "bar");
    i = 0;
    table2.visit([&](Symbol sym, uint32_t) {
        ++i;
        return true;
    });
    CHECK(i == 2 + NumSymbols);

    cout << table2 << endl;
}
