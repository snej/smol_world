//
// Heap.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#include "Heap.hh"
#include "Val.hh"
#include "Collections.hh"
#include <deque>
#include <iostream>

static constexpr uint32_t kMagic = 0xD217904A;

struct Header {
    uint32_t magic; // Must equal kMagic
    Val      root;  // Pointer to root object
};

static thread_local Heap const* sCurHeap;


Heap::Heap(void *base, size_t capacity, bool malloced)
:_base((byte*)base)
,_end(_base + capacity)
,_cur(_base)
,_malloced(malloced)
{
    assert(capacity >= sizeof(Header));
}


bool Heap::resize(size_t newSize) {
    if (newSize < used())
        return false;
    if (_malloced && newSize > capacity())
        return false;
    _end = _base + newSize;
    return true;
}

void Heap::reset() {
    _cur = _base;
    auto header = (Header*)alloc(sizeof(Header));
    *header = {kMagic, 0};
}


Heap Heap::existing(void *base, size_t used, size_t capacity) {
    Heap heap(base, capacity, false);
    heap._cur += used;
    auto header = (Header*)heap._base;
    if (header->magic != kMagic) {
        std::cout << "Invalid Heap: wrong magic number\n";
        return Heap();
    }
    if (header->root.isObject()) {
        heappos rootPos = header->root.asPos();
        if (rootPos < sizeof(Header) || rootPos >= used) {
            std::cout << "Invalid Heap: bad root offset\n";
            return Heap();
        }
    }
    return heap;
}

bool Heap::validPos(heappos pos) const    {return pos >= sizeof(Header) && pos < used();}


Val Heap::rootVal() const           {return ((Header*)_base)->root;}
void Heap::setRoot(Val val)         {((Header*)_base)->root = val;}
Object* Heap::rootObject() const    {return rootVal().asObject(this);}
void Heap::setRoot(Object* obj)     {setRoot(obj->asVal(this));}

Heap const* Heap::enter() const     {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const  {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::current()               {return (Heap*)sCurHeap;}

Object* Heap::firstObject() {
    return (Object*)(_base + sizeof(Header));
}

Object* Heap::nextObject(Object *obj) {
    obj = obj->nextObject();
    return (byte*)obj < _cur ? obj : nullptr;
}


void Heap::visit(Visitor const& visitor) {
    for (auto obj = firstObject(); obj; obj = nextObject(obj))
        obj->clearVisited();

    std::deque<Object*> stack;

    auto process = [&](Val val) -> bool {
        if (val.isObject()) {
            Object *obj = val.asObject(this);
            if (!obj->isVisited()) {
                obj->setVisited();
                if (!visitor(obj))
                    return false;
                if (Object::typeContainsPointers(obj->type()) && obj->dataSize() > 0)
                    stack.push_back(obj);
            }
        }
        return true;
    };

    if (!process(rootVal()))
        return;
    while (!stack.empty()) {
        Object *obj = stack.front();
        stack.pop_front();
        switch (obj->type()) {
            case Type::Array:
                for (Val v : *obj->as<Array>()) {
                    if (!process(v))
                        return;
                }
                break;
            case Type::Dict:
                for (DictEntry const& e : *obj->as<Dict>()) {
                    if (!process(e.key) || !process(e.value))
                        return;
                }
                break;
            default:
                break;
        }
    }
}



#pragma mark - GARBAGE COLLECTION:


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
    _toHeap.reset();
    scanRoot();
}


void GarbageCollector::scanRoot() {
#ifndef NDEBUG
    for (auto obj = _fromHeap.firstObject(); obj; obj = _fromHeap.nextObject(obj))
        assert(!obj->isForwarded());
#endif
    _toHeap.setRoot(scan(_fromHeap.rootVal()));
}


Val GarbageCollector::scan(Val val) {
    if (val.isObject()) {
        Object *obj = val.asObject(_fromHeap);
        return Val(scan(obj), _toHeap);
    } else {
        return val;
    }
}


Object* GarbageCollector::scan(Object *srcObj) {
    if (srcObj->isForwarded()) {
        return (Object*)_toHeap.at(srcObj->getForwardingAddress());
    } else {
        Type type = srcObj->type();
        auto dataSize = srcObj->dataSize();

        // Allocate an object of the same type & size in the destination heap:
        Object *dstObj = new (_toHeap, srcObj->dataSize()) Object(dataSize, type);
        assert(dstObj->dataSize() == srcObj->dataSize());
        auto fwdPos = _toHeap.pos(dstObj);

        // Copy the object's data into it:
        auto src = (const Val*)srcObj->dataPtr();
        auto dst = (Val*)dstObj->dataPtr();
        if (Object::typeContainsPointers(type)) {
            // `obj`s data is a sequence of `Val`s.
            // Recursively scan each one, storing the results in `dstObj`.
            assert(dataSize % sizeof(Val) == 0);
            if (int count = dataSize / sizeof(Val); count > 0) {
                // During the recursive scan, pointers to `obj` need to be forwarded to `dstObj`.
                // But setting `obj`s forwarding address overwrites 4 bytes, which will overwrite
                // the first 2 bytes of data if `obj` is small.
                // To work around this, read the first `Val` from `obj` before forwarding.
                auto firstItem = *src;
                srcObj->setForwardingAddress(fwdPos);
                *dst = scan(firstItem);
                while (--count > 0)
                    *++dst = scan(*++src);
            }
        } else {
            // If `obj` does not contain pointers, just do a memcpy:
            ::memcpy(dst, src, dataSize);
            srcObj->setForwardingAddress(fwdPos);
        }
        return dstObj;
    }
}


void GarbageCollector::update(Val* val) {
    *val = scan(*val);
}



void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}
