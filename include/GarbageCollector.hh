//
// GarbageCollector.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Heap.hh"
#include "Val.hh"
#include "Value.hh"
#include <deque>

namespace snej::smol {

/// A typical copying garbage collector that copies all live objects into another Heap.
/// At the end it swaps the memory of the two Heaps, so the original heap is now clean,
/// and the other heap can be freed or reused for the next GC.
class GarbageCollector {
public:
    static void run(Heap &heap) {
        GarbageCollector gc(heap);
    }

    static void run(Heap &heap, Heap &otherHeap) {
        GarbageCollector gc(heap, otherHeap);
    }

    /// Installs a callback in the Heap that will run GC when it fills up.
    static void runOnDemand(Heap &heap);

    /// Constructs the GC and copies all Values reachable from the root into a temporary Heap
    /// with the same capacity as this one.
    GarbageCollector(Heap &heap);

    /// Constructs the GC and copies all Values reachable from the root into `otherHeap`.
    GarbageCollector(Heap &heap, Heap &otherHeap);

    /// Updates an existing Val that came from the "from" heap,
    /// returning an equivalent Val that's been copied to the "to" heap.
    /// You MUST call this, or any of the `update` methods below,
    /// on any live references to values in `fromHeap`, or they'll be out of date.
    ///
    /// Do not do anything else with the heap while the GarbageCollector is in scope!
    Value scan(Value v);

    // These are equivalent to scanValue but update the Val/Ptr/Block in place:
    void update(Val*);
    void update(Value&);
    void update(Object&);

    ~GarbageCollector();

private:
    void scanRoots();
    Block* scan(Block*, Type) ;
    Block* moveBlock(Block*, Type);

    std::unique_ptr<Heap> _tempHeap;    // Owns temporary heap, if there is one
    Heap &_fromHeap, &_toHeap;          // The source and destination heaps
    std::deque<Block*> _toScan;              // Values in fromHeap
};

}
