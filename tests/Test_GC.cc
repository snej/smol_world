//
// Test_GC.cc
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


TEST_CASE("GC", "[gc]") {
    Heap heap(10000);
    UsingHeap u(heap);

    auto originalUsed = heap.used();
    cout << "Original heap used: " << originalUsed << endl;

    for (int i = 0; i < 100; ++i)
        String::create("Hello smol world!", heap);
    CHECK(heap.used() > originalUsed);

    GarbageCollector::run(heap);

    CHECK(heap.used() == originalUsed);

    Handle<Array> a = Array::create(100, heap);
    heap.setRoot(*a);
    for (int i = 0; i < 100; ++i)
        (*a)[i] = Val(String::create("Hello smol world!", heap), heap);
    auto laterUsed = heap.used();
    cout << "After allocating: " << laterUsed << endl;

    GarbageCollector::run(heap);
    cout << "After GC: " << heap.used() << endl;
    CHECK(heap.used() == laterUsed);

    (*a)[10] = nullval;
    GarbageCollector::run(heap);
    cout << "After GC: " << heap.used() << endl;
    CHECK(heap.used() < laterUsed);
}


TEST_CASE("GC On Demand", "[gc]") {
    Heap heap(10000);
    UsingHeap u(heap);
    heap.setAllocFailureHandler([](Heap* heap, heapsize sizeNeeded) {
        GarbageCollector::run(*heap);
        return heap->available() >= sizeNeeded;
    });
}
