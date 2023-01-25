//
// GarbageCollector.cc
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

#include "GarbageCollector.hh"
#include "Heap.hh"
#include "Block.hh"
#include "Val.hh"
#include "Object.hh"
#include <iostream>


GarbageCollector::GarbageCollector(Heap &heap)
:_tempHeap(std::make_unique<Heap>(heap.capacity()))
,_fromHeap(heap)
,_toHeap(*_tempHeap)
{
    scanRoot();
}


GarbageCollector::GarbageCollector(Heap &fromHeap, Heap &toHeap)
:_fromHeap(fromHeap), _toHeap(toHeap)
{
    scanRoot();
}


// The destructor swaps the two heaps, so _fromHeap is now the live one.
GarbageCollector::~GarbageCollector() {
    _fromHeap.reset();
    _fromHeap.swapMemoryWith(_toHeap);
}



void GarbageCollector::scanRoot() {
#ifndef NDEBUG
    for (auto obj = _fromHeap.firstBlock(); obj; obj = _fromHeap.nextBlock(obj))
        assert(!obj->isForwarded());
#endif
    _toHeap.reset();
    _toHeap.setRoot(scan(_fromHeap.rootVal()));
    _toHeap.setSymbolTableVal(scan(_fromHeap.symbolTableVal())); // TODO: Scan buckets as weak references to Symbols
    for (Object *refp : _fromHeap._externalRoots)
        update(*refp);
}


Val GarbageCollector::scan(Val val) {
    if (val.isObject()) {
        Block *block = val.asBlock(_fromHeap);
        return Val(scan(block), _toHeap);
    } else {
        return val;
    }
}


void GarbageCollector::update(Object& obj) {
    if (obj) {
        Block* dstBlock = scan(obj.block());
        obj.relocate(dstBlock);
    }
}


// First moves a live block `src` from fromHeap to toHeap (leaving a forwarding address behind.)
// Then it scans through its interior Vals (if any):
// any object pointed to by a Val is moved to toHeap and its pointer Val updated.
// This scan proceeds through any subsequent Blocks in toHeap that are appended to it by the moves.
// On completion, the `src` block and any blocks it transitively references are fully moved.
Block* GarbageCollector::scan(Block *src) {
    Block *toScan = (Block*)_toHeap._cur;
    Block *dst = move(src);
    while (toScan < (Block*)_toHeap._cur) {
        // Scan the contents of `toScan`:
        for (Val &v : toScan->vals()) {
            // Note: v is in toHeap, but was memcpy'd from fromHeap,
            // so any address in it is still relative to fromHeap.
            if (Block *b = v.asBlock(_fromHeap))
                v = Val(move(b), _toHeap);
        }
        // And advance it to the next block in _toHeap:
        toScan = toScan->nextBlock();
    }
    return dst;
}


// Moves a Block from _fromHeap to _toHeap, without altering its contents.
// - If the Block has already been moved, just returns the new location.
// - Otherwise copies (appends) it to _toHeap, then overwrites it with the forwarding address.
Block* GarbageCollector::move(Block* src) {
    if (src->isForwarded()) {
        return (Block*)_toHeap.at(src->getForwardingAddress());
    } else {
        auto size = src->blockSize();
        auto dst = (Block*)_toHeap.rawAlloc(size);
        ::memcpy(dst, src, size);
        src->setForwardingAddress(_toHeap.pos(dst));
        return dst;
    }
}


void GarbageCollector::update(Val* val) {
    *val = scan(*val);
}



void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}
