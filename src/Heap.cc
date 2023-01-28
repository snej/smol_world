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

struct Heap::Header {
    uint32_t magic;   // Must equal kMagic
    heappos  root;    // Pointer to root object
    heappos  symbols; // Pointer to symbol table
};

const size_t Heap::Overhead = sizeof(Header);

static thread_local Heap const* sCurHeap;

static std::vector<Heap*> sKnownHeaps;

Heap::Heap(void *base, size_t capacity, bool malloced)
:_base((byte*)base)
,_end(_base + capacity)
,_cur(_base)
,_malloced(malloced)
{
    assert(capacity >= sizeof(Header));
    registr();
}

Heap::~Heap() {
    assert(this != current());
    if (_malloced) free(_base);
    unregistr();
}


Heap::Heap()                                    :_base(nullptr), _end(nullptr), _cur(nullptr) { }
Heap::Heap(void *base, size_t cap) noexcept     :Heap(base, cap, false) {reset();}
Heap::Heap(size_t cap)                          :Heap(::malloc(cap), cap, true) {reset();}

Heap::Heap(Heap&& h) noexcept {
    *this = std::move(h);
}

Heap& Heap::operator=(Heap&& h) noexcept {
    unregistr();
    _base = h._base;
    _end = h._end;
    _cur = h._cur;
    _malloced = h._malloced;
    h._malloced = false;
    registr();
    h.unregistr();
    _allocFailureHandler = h._allocFailureHandler;
    _symbolTable = std::move(h._symbolTable);
    if (_symbolTable) _symbolTable->setHeap(this);    // <- this is the only non-default bit
    _externalRoots = std::move(h._externalRoots);
    return *this;
}

void Heap::swapMemoryWith(Heap &h) {
    std::swap(_base, h._base);
    std::swap(_end, h._end);
    std::swap(_cur, h._cur);
    std::swap(_malloced, h._malloced);
    std::swap(_symbolTable, h._symbolTable);
    if (_symbolTable) _symbolTable->setHeap(this);
    if (h._symbolTable) h._symbolTable->setHeap(&h);
    // _allocFailureHandle and _externalRoots are not swapped, they belong to the Heap itself.
}

void Heap::registr() {
    if (_base)
        sKnownHeaps.push_back(this);
}

void Heap::unregistr() {
    if (_base) {
        sKnownHeaps.erase(std::find(sKnownHeaps.begin(), sKnownHeaps.end(), this));
        _base = nullptr;
    }
}

Heap* Heap::heapContaining(const void *ptr) {
    for (Heap* h : sKnownHeaps)
        if (h->contains(ptr))
            return h;
    return nullptr;
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
    *header = {kMagic, nullpos, nullpos};
    _symbolTable.reset();
}


Heap Heap::existing(slice<byte> contents, size_t capacity) {
    assert(contents.size() > sizeof(Header));
    assert(capacity >= contents.size());
    Heap heap(contents.begin(), capacity, false);
    heap._cur = contents.end();

    auto header = heap.header();
    if (header.magic != kMagic) {
        std::cout << "Invalid Heap: wrong magic number\n";
        return Heap();
    }
    if (heappos rootPos = header.root; rootPos != nullpos) {
        if (rootPos < sizeof(Header) || rootPos >= heap.used()) {
            std::cout << "Invalid Heap: bad root offset\n";
            return Heap();
        }
    }
    return heap;
}

bool Heap::validPos(heappos pos) const    {return pos >= sizeof(Header) && pos < used();}

Value Heap::posToValue(heappos pos) const {
    if (pos == nullpos)
        return nullptr;
    return Value((Block*)at(pos), this);
}

heappos Heap::valueToPos(Value const& obj) const {
    return obj.isObject() ? obj.asPos(this) : nullpos;
}


Maybe<Object> Heap::root() const                {return posToValue(header().root).maybeAs<Object>();}
void Heap::setRoot(Maybe<Object> root)          {header().root = valueToPos(root);}
Value Heap::symbolTableArray() const            {return posToValue(header().symbols);}
void Heap::setSymbolTableArray(Value const& v)  {header().symbols = valueToPos(v);}


Heap const* Heap::enter() const     {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const  {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::current()               {return (Heap*)sCurHeap;}


SymbolTable& Heap::symbolTable() {
    if (!_symbolTable) {
        if_let(symbols, symbolTableArray().maybeAs<Array>()) {
            _symbolTable = std::make_unique<SymbolTable>(this, symbols);
        } else {
            _symbolTable = SymbolTable::create(this);
        }
        //FIXME: What if this fails?
    }
    return *_symbolTable;
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


void Heap::visitAll(BlockVisitor const& visitor) {
    for (auto b = firstBlock(); b; b = nextBlock(b))
        if (!visitor(*b))
            break;
}

void Heap::visitBlocks(BlockVisitor visitor) {
    for (auto obj = firstBlock(); obj; obj = nextBlock(obj))
        obj->clearVisited();

    std::deque<Block*> stack;

    auto processBlock = [&](Block *b) -> bool {
        if (!b->isVisited()) {
            b->setVisited();
            if (!visitor(*b))
                return false;
            if (Block::typeContainsPointers(b->type()) && b->dataSize() > 0)
                stack.push_back(b);
        }
        return true;
    };

    if_let(rootObj, root()) {
        if (!processBlock(rootObj.block()))
            return;
    }

    while (!stack.empty()) {
        Block *b = stack.front();
        stack.pop_front();
        for (Val const& val : b->vals()) {
            if (val.isObject() && !processBlock(val.asBlock(this)))
                return;
        }
    }
}


void Heap::visit(ObjectVisitor visitor) {
    visitBlocks([&](Block const& block) { return visitor(Object(&block,this)); });
}


void Heap::registerExternalRoot(Value *ref) const {
    assert(!ref->isObject() || contains(ref->asObject().rawBytes().begin()));
    _externalRoots.push_back(ref);
}

void Heap::unregisterExternalRoot(Value* ref) const {
    auto i = std::find(_externalRoots.rbegin(), _externalRoots.rend(), ref);
    assert(i != _externalRoots.rend());
    _externalRoots.erase(std::prev(i.base()));
}
