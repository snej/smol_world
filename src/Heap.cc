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
    h._malloced = false;
    _allocFailureHandler = h._allocFailureHandler;
    _symbolTable = std::move(h._symbolTable);
    _symbolTable->setHeap(this);    // <- this is the only non-default bit
    _externalRoots = std::move(h._externalRoots);
    return *this;
}

void Heap::swapMemoryWith(Heap &h) {
    std::swap(_base, h._base);
    std::swap(_end, h._end);
    std::swap(_cur, h._cur);
    std::swap(_malloced, h._malloced);
    std::swap(_symbolTable, h._symbolTable);
    _symbolTable->setHeap(this);
    h._symbolTable->setHeap(&h);
    // _allocFailureHandle and _externalRoots are not swapped, they belong to the Heap itself.
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
void Heap::setRoot(Object obj)      {((Header*)_base)->root = Val(obj, this);}

Object Heap::rootObject() const  {return rootVal().asObject(this);}

Heap const* Heap::enter() const     {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const  {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::current()               {return (Heap*)sCurHeap;}

Val Heap::symbolTableVal() const        {return ((Header*)_base)->symbols;}

void Heap::setSymbolTableVal(Val v)     {
    ((Header*)_base)->symbols = v;
    _symbolTable->setTable(v);
}


void* Heap::rawAllocFailed(heapsize size) {
    auto avail = available();
    if (_allocFailureHandler) {
        while(true) {
            std::cerr << "** Heap full: " << size << " bytes requested, only "
                      << avail << " available -- invoking failure handler **\n";
            if (!_allocFailureHandler(this, size))
                break;
            auto oldAvail = avail;
            avail = available();
            if (avail <= oldAvail) {
                std::cerr << "** Failure handler was unable to increase free space!\n";
                break;
            }
            std::cerr << "** Heap failure handler freed up " << (avail-oldAvail) << " bytes.\n";

            // retry the alloc:
            byte *result = _cur;
            byte *newCur = result + size;
            if (newCur <= _end) {
                _cur = newCur;
                return result;
            }
        }
    }
    std::cerr << "** Heap allocation failed: " << size << " bytes requested, only "
              << avail << " available **\n";
    return nullptr;
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
    assert(ref->rawBytes().null() || contains(ref->rawBytes().begin()));
    _externalRoots.push_back(ref);
}

void Heap::unregisterExternalRoot(Object* ref) const {
    auto i = std::find(_externalRoots.rbegin(), _externalRoots.rend(), ref);
    assert(i != _externalRoots.rend());
    _externalRoots.erase(std::prev(i.base()));
}
