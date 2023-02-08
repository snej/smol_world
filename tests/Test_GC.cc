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
using namespace snej::smol;


TEST_CASE("GC", "[gc]") {
    Heap heap(1000);
    UsingHeap u(heap);
    constexpr int kNumStrings = 5;

    auto gc = [&] {
        cout << "__________ BEFORE GC __________\n";
        heap.dump(cout);
        GarbageCollector::run(heap);
        cout << "__________ AFTER GC __________\n";
        heap.dump(cout);
    };

    auto originalUsed = heap.used();
    cout << "Original heap used: " << originalUsed << endl;

    for (int i = 0; i < kNumStrings; ++i)
        newString("Hello smol world!", heap);
    CHECK(heap.used() > originalUsed);

    gc();

    CHECK(heap.used() == originalUsed);

    Handle<Array> a = newArray(kNumStrings, heap).value();
    heap.setRoot(a);
    for (int i = 0; i < kNumStrings; ++i)
        a[i] = newString("Hello smol world!", heap);
    auto laterUsed = heap.used();
    cout << "After allocating: " << laterUsed << endl;

    gc();

    cout << "After GC: " << heap.used() << endl;
    CHECK(heap.used() == laterUsed);

    a[kNumStrings - 1] = nullval;

    gc();

    cout << "After GC: " << heap.used() << endl;
    CHECK(heap.used() < laterUsed);
}


TEST_CASE("GC On Demand", "[gc]") {
    Heap heap(100000);
    UsingHeap u(heap);
    heap.setAllocFailureHandler([](Heap* heap, heapsize sizeNeeded) {
        cout << "** GC **\n";
        GarbageCollector::run(*heap);
        heap->dump(cout); //TEMP
        return heap->available() >= sizeNeeded;
    });

    Handle<Array> a = newArray(500, nullishval, heap).value();
    for (int i = 0; i < 500; ++i)
        CHECK(a[i] == nullishval);
    heap.setRoot(a);
    for (int i = 0; i < 500; ++i) {
        //cout << "Blob #" << i << " -- used " << heap.used() << " free " << heap.available() << endl;
        auto blob = newBlob(1000, heap);
        REQUIRE(blob);
        (a)[i] = blob;
        if (i >= 50)
            a[i-50] = nullishval;
    }
    cout << "End -- used " << heap.used() << " free " << heap.available() << endl;
}
