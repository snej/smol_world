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
#include "smol_world.hh"
#include "Value.hh"
#include <iostream>

namespace snej::smol {

void GarbageCollector::runOnDemand(Heap &heap) {
    heap.setAllocFailureHandler([](Heap* heap, heapsize sizeNeeded, bool gcAllowed) {
        if (gcAllowed)
            GarbageCollector::run(*heap);
        return heap->available() >= sizeNeeded;
    });
}

GarbageCollector::GarbageCollector(Heap &heap)
:_tempHeap(std::make_unique<Heap>(heap.capacity()))
,_fromHeap(heap)
,_toHeap(*_tempHeap)
{
    _tempHeap->_iterable = _fromHeap._iterable;
    scanRoots();
}


GarbageCollector::GarbageCollector(Heap &fromHeap, Heap &toHeap)
:_fromHeap(fromHeap), _toHeap(toHeap)
{
    assert(_fromHeap._iterable == _toHeap._iterable);
    scanRoots();
}


// The destructor swaps the two heaps, so _fromHeap is now the live one.
GarbageCollector::~GarbageCollector() {
    _fromHeap.swapMemoryWith(_toHeap);
}



void GarbageCollector::scanRoots() {
    assert(!_fromHeap._cannotGC);

#ifndef NDEBUG
    if (_fromHeap._iterable) {
        for (auto obj = _fromHeap.firstBlock(); obj; obj = _fromHeap.nextBlock(obj))
            assert(!obj->isForwarded());
    }
#endif
    _toHeap.reset();
    _toHeap.setRoot(scan(_fromHeap.root()).maybeAs<Object>());
    for (Object *refp : _fromHeap._externalRootObjs)
        update(*refp);
    for (Value *refp : _fromHeap._externalRootVals)
        update(*refp);
    // (The SymbolTable registers root(s), so it will get updated implicitly.)
}


void GarbageCollector::update(Value& val) {
    if (val.isObject())
        val = scan(val);
}


void GarbageCollector::update(Object& obj) {
    if (obj.block())
        obj.relocate(scan(obj.block(), obj.type()));
}


Value GarbageCollector::scan(Value srcVal) {
    Block *src = srcVal.block();
    if (!src)
        return srcVal;
    Type type = srcVal.type();
    Block *dst = scan(src, type);
    return Value(dst, type);
}


// First moves a live block `src` from fromHeap to toHeap (leaving a forwarding address behind.)
// Then it scans through its interior Vals (if any):
// any object pointed to by a Val is moved to toHeap and its pointer Val updated.
// This scan proceeds through any subsequent Blocks in toHeap that are appended to it by the moves.
// On completion, the `src` block and any blocks it transitively references are fully moved.
Block* GarbageCollector::scan(Block *src, Type type) {
    Block *dst = moveBlock(src, type);
    while (!_toScan.empty()) {
        Block *toScan = _toScan.front();
        _toScan.pop_front();
        if (IsContainer(type)) {
            // Scan & update the contents of the Object in `toScan`:
            //std::cerr << "**** Scanning Vals in moved block " << (void*)toScan << "\n";
            for (Val &v : slice_cast<Val>(toScan->data())) {
                if (v.isObject()) {
                    // Note: v is in toHeap, but was memcpy'd from fromHeap,
                    // so any address in it is still relative to fromHeap.
                    auto block = (Block*)_fromHeap.at(heappos(v.decode()));
                    v.set(moveBlock(block, v.type()), v.type());
                }
            }
        }
    }
    return dst;
}


// Moves a Block from _fromHeap to _toHeap, without altering its contents.
// - If the Block has already been moved, just returns the new location.
// - Otherwise copies (appends) it to _toHeap, then overwrites it with the forwarding address.
Block* GarbageCollector::moveBlock(Block* src, Type type) {
    if (src->isForwarded()) {
        // Already forwarded! Just return the new address in _toHeap:
        return (Block*)_toHeap.at(src->forwardingAddress());
    } else {
        Block *dst;
        if (TypeIs(type, TypeSet::Container)) {
            slice<Val> vals = slice_cast<Val>(src->data());
            dst = _toHeap.allocBlock(vals.size() * sizeof(Val), type);
            //std::cerr << "**** Move container block " << (void*)src << " to " << (void*)dst << " -- " << _toHeap._cur << "\n";
            // Ugh. We have to move a bunch of relative-pointers, which still need to resolve to
            // their original addresses until they get processed during the loop in scan().
            // But there's no guarantee toHeap is within 2GB of fromHeap, so they're not capable
            // of pointing back to it.
            // The workaround is to transform each pointer-based value into a pointer to the
            // equivalent heap offset. So if the original Val pointed to fromHeap+3F8, the copied
            // Val points to toHeap+3F8. This isn't a useable Val, but scan() can undo this.
            auto dstItem = (uintpos*)dst;
            for (Val const& srcVal : vals) {
                if (srcVal.isObject())
                    *dstItem++ = Val::encode(srcVal.type(), uintpos(_fromHeap.pos(srcVal._block())));
                else
                    *dstItem++ = (uintpos&)srcVal;
            }
            _toScan.push_back(dst);
        } else {
            // Moving a block of non-Vals just memcpy:
            auto [withHeader, headerSize] = src->withHeader();
            if (_fromHeap._iterable) {
                withHeader = withHeader.moveStart(-1);
                ++headerSize;
            }
            auto dstHeader = (byte*)_toHeap.rawAlloc(withHeader.sizeInBytes());
            withHeader.memcpyTo(dstHeader);
            dst = (Block*)(dstHeader + headerSize);
            //std::cerr << "---- Move data block " << (void*)src << " to " << (void*)dst << " -- " << _toHeap._cur << "\n";
        }
        // Finally replace the block with a forwarding address (overwriting part of its contents):
        src->setForwardingAddress(_toHeap.pos(dst));
        return dst;
    }
}


void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}

}
