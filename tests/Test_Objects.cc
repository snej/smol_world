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
#include <array>
#include <iostream>
#include <sstream>

using namespace std;
using namespace snej::smol;


template <ObjectClass OBJ>
static OBJ mustHave(Maybe<OBJ> const& m) {
    REQUIRE(m);
    return m.value();
}


static void checkTypes(Value v, Type t, string_view asString) {
    INFO("checking type " << t << " value " << asString);
    CHECK(v.type() == t);
    CHECK(bool(v) == (v != nullvalue));
    CHECK((v.isNull() || v.isNullish()) == (t == Type::Null));
    CHECK(v.isBool() == (t == Type::Bool));
    CHECK(v.isInt() == (t == Type::Int));
    CHECK(v.isObject() == (t < Type::Null));

    stringstream s;
    s << v;
    CHECK(s.str() == asString);
}

static void checkInt(int n) {
    auto i = Int(n);
    checkTypes(i, Type::Int, std::to_string(n));
    CHECK(i.asInt() == n);
    CHECK(i == n);
}


TEST_CASE("Primitive Values", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);

    checkTypes(Null(), Type::Null, "null");
    CHECK(Null() == nullvalue);

    checkTypes(nullishvalue, Type::Null, "nullish");

    checkTypes(Bool(false), Type::Bool, "false");
    checkTypes(Bool(true), Type::Bool, "true");
    CHECK(Bool(false) != Null());
    CHECK(Bool(false) != Int(0));
    CHECK(Bool(true) != Int(1));

    for (int n = -10000; n <= 10000; ++n)
        checkInt(n);
    for (int n = 0; n < 100; ++n) {
        checkInt(Val::MaxInt - n);
        checkInt(Val::MinInt + n);
    }
}


TEST_CASE("Numbers", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);

    CHECK_FALSE(Bool(1).isNumber());
    CHECK(Int(1).isNumber());
    CHECK(Int(1).asNumber<float>() == 1.0f);

    BigInt i = newBigInt(1234567890123, heap).value();
    checkTypes(i, Type::BigInt, "1234567890123");
    CHECK(i.isNumber());
    CHECK(i.asInt() == 1234567890123);
    CHECK(i.asNumber<int32_t>() == INT32_MAX);
    CHECK(i.asNumber<uint64_t>() == 1234567890123u);

    i = newBigInt(-1234567890123, heap).value();
    CHECK(i.isNumber());
    CHECK(i.asInt() == -1234567890123);
    CHECK(i.asNumber<int32_t>() == INT32_MIN);
    CHECK(i.asNumber<uint64_t>() == 0);

    Float f = newFloat(3.14159, heap).value();
    checkTypes(f, Type::Float, "3.14159");
    CHECK(f.isNumber());
    CHECK(f.asDouble() == 3.14159);
    CHECK(f.asFloat() == 3.14159f);
}


TEST_CASE("Strings", "[object]") {
    constexpr string_view kString = "Hello, smol world!";
    Heap heap(1000);
    UsingHeap u(heap);

    for (int len = 0; len <= kString.size(); ++len) {
        INFO("len is " << len);
        string_view nativeStr = kString.substr(0, len);
        unless(str, newString(nativeStr, heap)) {FAIL("Failed to alloc String");}
        REQUIRE(str.type() == Type::String);
        REQUIRE(str.is<String>());
        Value val = str;
        CHECK(val.type() == Type::String);
        CHECK(val.as<String>() == str);

        CHECK(str.capacity() == len);
        CHECK(str.size() == len);
        CHECK(str.empty() == (len == 0));
        CHECK(str.str() == nativeStr);
        CHECK(string(str.begin(), str.end()) == nativeStr);
    }
}


TEST_CASE("Maybe", "[object]") {
    Heap heap(1000);
    if_let (str2, newString("maybe?", heap)) {
        CHECK(str2.str() == "maybe?");

        if_let (arr, str2.maybeAs<Array>()) {
            FAIL("MAYBE false positive");
        }
    } else {
        FAIL("MAYBE false negative");
    }
}


TEST_CASE("Blobs", "[object]") {
    constexpr array<uint8_t,10> kBlob = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    Heap heap(1000);
    UsingHeap u(heap);

    for (int len = 0; len <= kBlob.size(); ++len) {
        INFO("len is " << len);
        unless(blob, newBlob(kBlob.data(), len, heap)) {FAIL("Failed to alloc object");}
        REQUIRE(blob.type() == Type::Blob);
        REQUIRE(blob.is<Blob>());
        Value val = blob;
        CHECK(val.as<Blob>() == blob);

        CHECK(blob.capacity() == len);
        CHECK(blob.size() == len);
        CHECK(blob.empty() == (len == 0));
        CHECK(memcmp(kBlob.begin(), blob.begin(), len) == 0);
    }
}


TEST_CASE("Arrays", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);

    Maybe<String> strs[10];
    for (int i = 0; i < 10; ++i)
        strs[i] = newString(std::to_string(i), heap);

    for (int len = 0; len <= 10; ++len) {
        INFO("len is " << len);
        unless(array, newArray(len, heap)) {FAIL("Failed to alloc object");}
        REQUIRE(array.type() == Type::Array);
        REQUIRE(array.is<Array>());
        Value val = array;
        CHECK(val.as<Array>() == array);

        CHECK(array.capacity() == len);
        CHECK(array.size() == len);

        for (int i = 0; i < len; ++i)
            array[i] = strs[i];
        for (int i = 0; i < len; ++i)
            CHECK(array[i].maybeAs<String>() == strs[i]);
    }
}


TEST_CASE("Vectors", "[object]") {
    Heap heap(1000);
    UsingHeap u(heap);

    Maybe<String> strs[10];
    for (int i = 0; i < 10; ++i)
        strs[i] = newString(std::to_string(i), heap);

    for (int capacity = 0; capacity <= 10; ++capacity) {
        INFO("capacity is " << capacity);
        unless(vec, newVector(capacity, heap)) {FAIL("Failed to alloc object");}
        REQUIRE(vec.type() == Type::Vector);
        REQUIRE(vec.is<Vector>());
        Value val = vec;
        CHECK(val.as<Vector>() == vec);

        CHECK(vec.capacity() == capacity);
        CHECK(vec.size() == 0);
        CHECK(vec.empty());
        CHECK(vec.full() == (capacity == 0));

        for (int i = 0; i < capacity; ++i) {
            vec.append(strs[i]);
            CHECK(vec[i] == strs[i]);
            CHECK(vec.size() == i + 1);
            CHECK(!vec.empty());
            CHECK(vec.full() == (i == (capacity-1)));
        }
        for (int i = 0; i < capacity; ++i)
            CHECK(vec[i].maybeAs<String>() == strs[i]);
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

    Maybe<Symbol> strs[11];
    for (int i = 0; i < 11; ++i) {
        strs[i] = newSymbol(std::to_string(i), heap);
        CHECK(strs[i].value().str() == std::to_string(i));
        CHECK(strs[i].value().id() == Symbol::ID(i));
    }
    shuffle(strs+0, strs+11);

    for (int len = 0; len <= 10; ++len) {
        cerr << "\n====len is " << len << endl;
        INFO("len is " << len);
        unless(dict, newDict(len, heap)) {FAIL("Failed to alloc object");}
        REQUIRE(dict.type() == Type::Dict);
        REQUIRE(dict.is<Dict>());
        Value val = dict;
        CHECK(val.as<Dict>() == dict);

        CHECK(dict.capacity() == len);
        CHECK(dict.empty());
        //dict.dump(cout);

        for (int i = 0; i <= len; ++i) {
            INFO("i = " << i);
            Symbol key = strs[i].value();
            CHECK(dict.size() == i);
            CHECK(dict.full() == (i == len));
            CHECK(!dict.contains(key));
            CHECK(!dict.replace(key, -1));
            if (i < len) {
                cerr << "----i is " << i << ", adding " << (void*)strs[i].value().block() << endl;
                CHECK(dict.set(key, i));
                //dict.dump(cout);
                CHECK(!dict.empty());
                CHECK(dict.contains(key));
                CHECK(dict.get(key) == i);
                CHECK(!dict.insert(key, -1));
                CHECK(dict.replace(key, -i));

                for (int j = 0; j < 10; ++j)
                    CHECK(dict.get(strs[j].value()) == ((j <= i) ? Value(-j) : Value()));

            } else {
                CHECK(!dict.set(key, i));
                CHECK(!dict.insert(key, -1));
                CHECK(!dict.contains(key));
            }
        }

        cout << ".... checking\n";
        shuffle(strs+0, strs+len);
        for (int i = 0; i < len; ++i) {
            Symbol key = strs[i].value();
            INFO("i = " << i << ", key = " << (void*)key.block());
            CHECK(dict.size() == len-i);
            CHECK(dict.full() == (i == 0));
            CHECK(dict.contains(key));
            CHECK(dict.remove(key));
            CHECK(!dict.contains(key));
            CHECK(!dict.remove(key));
            CHECK(dict.empty() == (i == len-1));
        }
    }
}


TEST_CASE("HashMap", "[object],[hash]") {
    Heap heap(10000);
    UsingHeap u(heap);
    Handle<Array> hashArray = mustHave( HashMap::createArray(heap, 50) );
    HashMap table(heap, hashArray);

    CHECK(&table.heap() == &heap);
    CHECK(table.array() == hashArray);
    CHECK(table.count() == 0);
    CHECK(table.capacity() >= 50);

    Handle<String> foo = mustHave( newString("foo", heap) );
    Handle<String> bar = mustHave( newString("bar", heap) );

    CHECK(table.get(foo) == nullptr);
    table.put(foo, 0xF00);
    CHECK(table.count() == 1);
    CHECK(table.get(foo) == 0xF00);

    CHECK(table.get(bar) == nullptr);
    table.put(bar, 0xBA4);
    CHECK(table.count() == 2);
    CHECK(table.get(bar) == 0xBA4);
    CHECK(table.get(foo) == 0xF00);

    table.dump(cout);
}


TEST_CASE("Symbols", "[object],[hash]") {
    Heap heap(10000);
    SymbolTable& table = heap.symbolTable();
    SymbolTable& tableAgain = heap.symbolTable();
    CHECK(&table == &tableAgain);
    CHECK(table.size() == 0);

    CHECK(table.find("foo") == nullval);

    if_let (foo, table.create("foo")) {
        CHECK(foo.str() == "foo");
        CHECK(table.find("foo") == foo);
    } else {
        FAIL("Couldn't create 'foo'");
    }

    unless(bar, table.create("bar")) {FAIL("Failed to create 'bar'");}
    CHECK(bar.str() == "bar");
    CHECK(table.find("bar") == bar);
    CHECK(table.size() == 2);

    constexpr size_t NumSymbols = 100;
    Maybe<Symbol> syms[NumSymbols];
    for (size_t i = 0; i < NumSymbols; ++i) {
        string name = "Symbol #" + std::to_string(i * i);
        cerr << "Creating #" << i << ": " << name << endl;
        CHECK(table.find(name) == nullval);
        unless(sym, table.create(name)) {FAIL("Failed to alloc sym");}
        syms[i] = sym;
        CHECK(sym.str() == name);
        CHECK(table.find(name) == sym);
        CHECK(table.size() == 3 + i);
    }
    for (size_t i = 0; i < NumSymbols; ++i) {
        string name = "Symbol #" + std::to_string(i * i);
        CHECK(table.find(name) == syms[i]);
    }

    size_t i = 0;
    table.visit([&](Symbol) {
        ++i;
        return true;
    });
    cerr << endl;
    CHECK(i == 2 + NumSymbols);

    // Open a heap from the current heap and check it has everything:
    Heap heap2 = Heap::existing(heap.contents(), heap.capacity());
    SymbolTable& table2 = heap2.symbolTable();
    CHECK(table2.size() == 2 + NumSymbols);
    unless(bar2, table2.find("bar"))  {FAIL("Failed to find 'bar'");}
    CHECK(bar2.str() == "bar");
    i = 0;
    table2.visit([&](Symbol) {
        ++i;
        return true;
    });
    CHECK(i == 2 + NumSymbols);

    cout << table2 << endl;
}
