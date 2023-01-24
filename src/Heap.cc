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

Object Heap::rootObject() const  {return rootVal().asObject(this);}

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
        for (Val v : b->vals())
            if (!process(v))
                return;
    }
}


void Heap::registerExternalRoot(Object *ref) const {
    _externalRoots.push_back(ref);
}

void Heap::unregisterExternalRoot(Object* ref) const {
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
    scanRoot();
}


void GarbageCollector::scanRoot() {
#ifndef NDEBUG
    for (auto obj = _fromHeap.firstBlock(); obj; obj = _fromHeap.nextBlock(obj))
        assert(!obj->isForwarded());
#endif
    _toHeap.reset();
    _toHeap.setRoot(scan(_fromHeap.rootVal()));
    _toHeap.setSymbolTableVal(scan(_fromHeap.symbolTableVal())); // TODO: Scan buckets as weak references to Symbols
    for (Object *refp : _fromHeap._externalRoots) {
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
    Block *toScan = (Block*)_toHeap._cur;
    Block *dstObj = move(srcObj);
    while (toScan < (Block*)_toHeap._cur) {
        // Scan the contents of `toScan`:
        for (Val &v : toScan->vals()) {
            if (Block *b = v.asBlock(_fromHeap))
                v = Val(move(b), _toHeap);
        }
        // And advance it to the next block in _toHeap:
        toScan = toScan->nextBlock();
    }
    return dstObj;
}


// Moves a Block from _fromHeap to _toHeap, without altering its contents.
// - If the Block has already been moved, returns the new location.
// - Otherwise copies (appends) it to _toHeap, then overwrites it with the forwarding address.
Block* GarbageCollector::move(Block* src) {
    if (src->isForwarded()) {
        return (Block*)_toHeap.at(src->getForwardingAddress());
    } else {
        auto size = src->blockSize();
        auto dst = (Block*)_fromHeap.rawAlloc(size);
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
