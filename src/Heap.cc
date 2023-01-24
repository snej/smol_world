//
// Heap.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#include "Heap.hh"
#include "Block.hh"
#include "Val.hh"
#include "Collections.hh"
#include "SymbolTable.hh"
#include <deque>
#include <iostream>

static constexpr uint32_t kMagic = 0xA189933A;

struct Header {
    uint32_t magic;   // Must equal kMagic
    Val      root;    // Pointer to root object
    Val      symbols; // Pointer to symbol table
};

const size_t Heap::Overhead = sizeof(Header);

static thread_local Heap const* sCurHeap;


Heap::Heap(void *base, size_t capacity, bool malloced)
:_base((byte*)base)
,_end(_base + capacity)
,_cur(_base)
,_malloced(malloced)
{
    assert(capacity >= sizeof(Header));
}

Heap::Heap()                                    :_base(nullptr), _end(nullptr), _cur(nullptr) { }
Heap::Heap(void *base, size_t cap) noexcept     :Heap(base, cap, false) {reset();}
Heap::Heap(size_t cap)                          :Heap(::malloc(cap), cap, true) {reset();}
Heap::Heap(Heap&& h) noexcept                   {*this = std::move(h);}
Heap::~Heap()                               {assert(this != current()); if (_malloced) free(_base);}

Heap& Heap::operator=(Heap&& h) noexcept {
    _base = h._base;
    _end = h._end;
    _cur = h._cur;
    _malloced = h._malloced;
    _allocFailureHandler = h._allocFailureHandler;
    _symbolTable = std::move(h._symbolTable);
    _symbolTable->setHeap(this);    // <- this is the only non-default bit
    _externalRoots = std::move(h._externalRoots);
    return *this;
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
    auto header = (Header*)rawAlloc(sizeof(Header));
    *header = {kMagic, nullval, nullval};
    if (_symbolTable)
        _symbolTable->setTable(nullval);
    else
        _symbolTable = std::make_unique<SymbolTable>(this, nullval);
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
    heap._symbolTable = std::make_unique<SymbolTable>(&heap, header->symbols);
    return heap;
}

bool Heap::validPos(heappos pos) const    {return pos >= sizeof(Header) && pos < used();}


Val Heap::rootVal() const           {return ((Header*)_base)->root;}
void Heap::setRoot(Val val)         {((Header*)_base)->root = val;}

Heap const* Heap::enter() const     {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const  {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::current()               {return (Heap*)sCurHeap;}

Val Heap::symbolTableVal() const        {return ((Header*)_base)->symbols;}

void Heap::setSymbolTableVal(Val v)     {
    ((Header*)_base)->symbols = v;
    _symbolTable->setTable(v);
}


void* Heap::alloc(heapsize size) {
    // As a general-purpose allocator we just allocate a raw Block and return its data.
    auto blob = Block::alloc(size, Type::Blob, this);
    return blob ? blob->dataPtr() : nullptr;
}


Block* Heap::firstBlock() {
    return (Block*)(_base + sizeof(Header));
}

Block* Heap::nextBlock(Block *b) {
    b = b->nextBlock();
    return (byte*)b < _cur ? b : nullptr;
}


void Heap::visitAll(Visitor const& visitor) {
    for (auto b = firstBlock(); b; b = nextBlock(b))
        if (!visitor(*b))
            break;
}

void Heap::visit(Visitor const& visitor) {
    for (auto obj = firstBlock(); obj; obj = nextBlock(obj))
        obj->clearVisited();

    std::deque<Block*> stack;

    auto process = [&](Val val) -> bool {
        if (val.isObject()) {
            Block *b = val.asBlock(this);
            if (!b->isVisited()) {
                b->setVisited();
                if (!visitor(*b))
                    return false;
                if (Block::typeContainsPointers(b->type()) && b->dataSize() > 0)
                    stack.push_back(b);
            }
        }
        return true;
    };

    if (!process(rootVal()))
        return;
    while (!stack.empty()) {
        Block *b = stack.front();
        stack.pop_front();
        switch (b->type()) {
            case Type::Array:
                for (Val v : *b->as<Array>()) {
                    if (!process(v))
                        return;
                }
                break;
            case Type::Dict:
                for (DictEntry const& e : *b->as<Dict>()) {
                    if (!process(e.key) || !process(e.value))
                        return;
                }
                break;
            default:
                break;
        }
    }
}


void Heap::registerExternalRoot(ObjectRef *ref) const {
    _externalRoots.push_back(ref);
}

void Heap::unregisterExternalRoot(ObjectRef* ref) const {
    auto i = std::find(_externalRoots.rbegin(), _externalRoots.rend(), ref);
    assert(i != _externalRoots.rend());
    _externalRoots.erase(i.base());
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
    for (auto obj = _fromHeap.firstBlock(); obj; obj = _fromHeap.nextBlock(obj))
        assert(!obj->isForwarded());
#endif
    _toHeap.setRoot(scan(_fromHeap.rootVal()));
    _toHeap.setSymbolTableVal(scan(_fromHeap.symbolTableVal())); // TODO: Scan buckets as weak references to Symbols
    for (ObjectRef *refp : _fromHeap._externalRoots) {
        Block* dstBlock = scan(refp->block());
        refp->relocate(dstBlock);
    }
}


Val GarbageCollector::scan(Val val) {
    if (val.isObject()) {
        Block *obj = val.asBlock(_fromHeap);
        return Val(scan(obj), _toHeap);
    } else {
        return val;
    }
}


Block* GarbageCollector::scan(Block *srcObj) {
    if (srcObj->isForwarded()) {
        return (Block*)_toHeap.at(srcObj->getForwardingAddress());
    } else {
        Type type = srcObj->type();
        auto dataSize = srcObj->dataSize();

        // Allocate an object of the same type & size in the destination heap:
        void* addr = Object::alloc(sizeof(Object), _toHeap, srcObj->dataSize());
        assert(addr);
        Object *dstObj = new (addr) Object(dataSize, type);
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
                // A Dict needs to re-sort its keys after a GC because the keys are sorted by
                // Val (address), and those addresses will be differently ordered in the new heap.
                if (type == Type::Dict)
                    ((Dict*)dstObj)->sort();
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
