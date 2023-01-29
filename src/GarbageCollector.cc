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
#include "Value.hh"
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
    _toHeap.setRoot(scan(_fromHeap.root()).maybeAs<Object>());
    _toHeap.setSymbolTableArray(scan(_fromHeap.symbolTableArray())); // TODO: Scan buckets as weak references to Symbols
    for (Value *refp : _fromHeap._externalRoots)
        update(*refp);
}


Value GarbageCollector::scan(Value val) {
    if (val.isObject()) {
        Block *block = val.block();
        return Value(scan(block));
    } else {
        return val;
    }
}


void GarbageCollector::update(Value& obj) {
    if (obj.isObject())
        obj.relocate(scan(obj.block()));
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
            if (Block *block = v.block()) {
                // Note: v is in toHeap, but was memcpy'd from fromHeap,
                // so any address in it is still relative to fromHeap.
                block = (Block*)_fromHeap.at(_toHeap._pos(block));   // translate it back to fromHeap
                v = Val(move(block));
            }
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
        Block *dst;
        if (src->containsVals()) {
            // Ugh. We have to move a bunch of relative-pointers, which still need to resolve to
            // their original addresses until they get processed during the loop in scan().
            // But there's no guarantee toHeap is within 2GB of fromHeap, so they're not capable
            // of pointing back to it.
            // The workaround is to transform each pointer-based value into a pointer to the
            // equivalent heap offset. So if the original Val pointed to fromHeap+3F8, the copied
            // Val points to toHeap+3F8. This isn't a useable Val, but scan() can undo this.
            dst = Block::alloc(src->dataSize(), src->type(), _toHeap);
            auto dstVal = (Val*)dst->dataPtr();
            for (Val const& srcVal : src->vals()) {
                if (srcVal.isObject())
                    *dstVal++ = (Block*)_toHeap._at(_fromHeap.pos(srcVal._block()));
                else
                    *dstVal++ = srcVal;
            }
        } else {
            auto size = src->blockSize();
            assert(size < 2000);//TEMP
            dst = (Block*)_toHeap.rawAlloc(size);
            ::memcpy(dst, src, size);
        }
        src->setForwardingAddress(_toHeap.pos(dst));
        return dst;
    }
}


void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}
