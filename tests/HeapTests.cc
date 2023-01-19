//
// HeapTests.cc
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


TEST_CASE("Empty Heap", "[heap]") {
    Heap heap(10000);
    // attributes:
    CHECK(heap.valid());
    CHECK(heap.base() != nullptr);
    CHECK(heap.capacity() == 10000);
    CHECK(heap.used() == 8);
    CHECK(heap.remaining() == 10000 - 8);

    CHECK(!heap.contains(nullptr));

    // roots:
    CHECK(heap.rootVal() == nullval);
    CHECK(heap.rootObject() == nullptr);

    // current heap:
    CHECK(Heap::current() == nullptr);
    {
        UsingHeap u(heap);
        CHECK(Heap::current() == &heap);
    }
    CHECK(Heap::current() == nullptr);

    // visit:
    heap.visit([&](const Object *obj) { FAIL("Visitor should not be called"); return false; });
}


TEST_CASE("Alloc", "[heap]") {
    Heap heap(10000);
    auto ptr = (byte*)heap.alloc(123);
    cout << "ptr = " << (void*)ptr << endl;
    REQUIRE(ptr != nullptr);
    CHECK(heap.contains(ptr));
    CHECK(heap.contains(ptr + 122));
    CHECK(!heap.contains(ptr + 123));

    CHECK(heap.used() == 8 + 2 + 123);
    CHECK(heap.remaining() == 10000 - 8 - 2 - 123);

    int i = 0;
    heap.visitAll([&](const Object *obj) {
        CHECK(heap.contains(obj));
        CHECK(obj->type() == Type::Blob);
        switch (i++) {
            case 0: CHECK(obj->dataSize() == 123); return true;
            default: FAIL("Invalid object visited"); return false;
        }
    });
    CHECK(i == 1);

    auto ptr2 = (byte*)heap.alloc(9863); // exactly fills the heap
    cout << "ptr2= " << (void*)ptr2 << endl;
    REQUIRE(ptr2 != nullptr);
    CHECK(heap.contains(ptr2));
    CHECK(heap.contains(ptr2 + 9862));
    CHECK(!heap.contains(ptr2 + 9863));

    CHECK(heap.used() == 10000);
    CHECK(heap.remaining() == 0);

    i = 0;
    heap.visitAll([&](const Object *obj) {
        CHECK(heap.contains(obj));
        CHECK(obj->type() == Type::Blob);
        switch (i++) {
            case 0: CHECK(obj->dataSize() == 123); return true;
            case 1: CHECK(obj->dataSize() == 9863); return true;
            default: FAIL("Invalid object visited"); return false;
        }
    });
    CHECK(i == 2);

    CHECK(heap.alloc(1) == nullptr);
}


TEST_CASE("Alloc Objects", "[heap]") {
    Heap heap(10000);

}
