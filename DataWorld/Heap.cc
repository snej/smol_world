//
// Heap.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#include "Heap.hh"
#include "Val.hh"
#include "Objects.hh"
#include <deque>

static constexpr uint32_t kMagic = 0xD217904A;

struct Header {
    uint32_t magic; // Must equal kMagic
    Val      root;  // Pointer to root object
};

static thread_local Heap* sCurHeap;


Heap::Heap(void *base, size_t capacity, bool malloced)
:_base((byte*)base)
,_end(_base + capacity)
,_cur(_base)
,_malloced(malloced)
{
    assert(isAligned(_base));
    assert(capacity >= sizeof(Header));
}

void Heap::reset() {
    _cur = _base;
    *alloc<Header>() = {kMagic, 0};
}


Heap Heap::existing(void *base, size_t used, size_t capacity) {
    Heap heap(base, capacity, false);
    heap._cur += used;
    auto header = (Header*)heap._base;
    if (header->magic != kMagic)
        throw std::runtime_error("Invalid Heap: wrong magic number");
    if (header->root.isObject()) {
        heappos rootPos = header->root.asPos();
        if (rootPos < sizeof(Header) || rootPos >= used)
            throw std::runtime_error("Invalid Heap: bad root offset");
    }
    return heap;
}

bool Heap::validPos(heappos pos) const    {return pos >= sizeof(Header) && pos < used();}


Val Heap::root() const              {return ((Header*)_base)->root;}
void Heap::setRoot(Val val)         {((Header*)_base)->root = val;}

void Heap::enter()                  {assert(!sCurHeap); sCurHeap = this;}
void Heap::exit()                   {assert(sCurHeap == this); sCurHeap = nullptr;}
Heap* Heap::current()               {return sCurHeap;}


Object* Heap::firstObject() {
    return (Object*)(_base + sizeof(Header));
}

Object* Heap::nextObject(Object *obj) {
    uintptr_t addr = (uintptr_t)obj + sizeof(Object) + obj->dataSize();
    obj = alignUp((Object*)addr);
    return (byte*)obj < _cur ? obj : nullptr;
}

void Heap::clearObjectFlags(heapsize /*Object::Flags*/ flags) {
    for (auto obj = firstObject(); obj; obj = nextObject(obj))
        obj->setFlag(Object::Flags(flags), false);
}


void Heap::visit(Visitor const& visitor) {
    clearObjectFlags(Object::Visited);
    std::deque<Val> stack;

    auto process = [&](Val val) -> bool {
        if (val.isObject()) {
            Object *obj = val.asObject(this);
            if (!obj->hasFlag(Object::Visited)) {
                obj->setFlag(Object::Visited, true);
                if (!visitor(val))
                    return false;
                if (val.isArray() || val.isDict())
                    stack.push_back(val);
            }
        }
        return true;
    };

    if (!process(root()))
        return;
    while (!stack.empty()) {
        Val val = stack.front();
        stack.pop_front();
        switch (val.type()) {
            case ValType::Array:
                for (Val v : *val.as<Array>(this)) {
                    if (!process(v))
                        return;
                }
                break;
            case ValType::Dict:
                for (DictEntry const& e : *val.as<Dict>(this)) {
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
    _fromHeap.clearObjectFlags(Object::Fwd);
    _toHeap.setRoot(scanValue(_fromHeap.root()));
}


Val GarbageCollector::scanValue(Val val) {
    switch (val.type()) {
        case ValType::String:   return scanValueAs<String>(val);
        case ValType::Array:    return scanValueAs<Array>(val);
        case ValType::Dict:     return scanValueAs<Dict>(val);
        default:                return val;
    }
}


template <class T>
Val GarbageCollector::scanValueAs(Val val) {
    T *obj = val.as<T>(_fromHeap);
    if (heappos fwd = obj->getForwardingAddress(); fwd > 0)
        return Val(fwd, T::Tag);
    auto capacity = obj->capacity();
    auto begin = obj->begin(), end = obj->end();
    T *dstObj = T::createUninitialized(capacity, _toHeap);
    Val dst(dstObj, _toHeap);
    obj->setForwardingAddress(dst.asPos());
    dstObj->populate(begin, end, [this](Val oldVal) {return scanValue(oldVal);});
    return dst;
}


void GarbageCollector::update(Val &val) {
    val = scanValue(val);
}



void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}
