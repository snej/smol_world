//
// Heap.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#include "Heap.hh"
#include "smol_world.hh"
#include <deque>
#include <iomanip>
#include <iostream>
#include <unordered_set>

namespace snej::smol {

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
    if (_symbolTable) _symbolTable->setHeap(*this);    // <- this is the only non-default bit
    _externalRootObjs = std::move(h._externalRootObjs);
    _externalRootVals = std::move(h._externalRootVals);
    return *this;
}

void Heap::swapMemoryWith(Heap &h) {
    std::swap(_base, h._base);
    std::swap(_end, h._end);
    std::swap(_cur, h._cur);
    std::swap(_malloced, h._malloced);
    std::swap(_symbolTable, h._symbolTable);
    if (_symbolTable) _symbolTable->setHeap(*this);
    if (h._symbolTable) h._symbolTable->setHeap(h);
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
    return Value((Block*)at(pos));
}

heappos Heap::valueToPos(Value obj) const {
    return obj.isObject() ? pos(obj.block()) : nullpos;
}


Maybe<Object> Heap::root() const                {return posToValue(header().root).maybeAs<Object>();}
void Heap::setRoot(Maybe<Object> root)          {header().root = valueToPos(root);}
Value Heap::symbolTableArray() const            {return posToValue(header().symbols);}
void Heap::setSymbolTableArray(Value v)         {header().symbols = valueToPos(v);}


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


void* Heap::rawAlloc(heapsize size) {
    byte *result = _cur;
    byte *newCur = result + size;
    if (_likely(newCur <= _end)) {
        _cur = newCur;
        return result;
    } else {
        return rawAllocFailed(size); // handle heap-full
    }
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
    if (Block *block = allocBlock(size, Type::Blob))
        return block->dataPtr();
    else
        return nullptr;
}


Block* Heap::allocBlock(heapsize size, Type type) {
    if (void* addr = rawAlloc(Block::sizeForData(size)))
        return new (addr) Block(size, type);
    else
        return nullptr;
}


Block* Heap::allocBlock(heapsize size, Type type, slice<byte> contents) {
    Block *block = allocBlock(size, type);
    if (block)
        block->fill(contents);
    return block;
}


Block* Heap::growBlock(Block* block, heapsize newDataSize) {
    auto data = block->data();
    if (newDataSize == data.size())
        return block;
    assert(newDataSize > data.size()); //TODO: Implement shrinking
    auto newBlock = allocBlock(newDataSize, block->type());
    if (!newBlock)
        return nullptr;
    if (auto vals = block->vals()) {
        // Vals are relative ptrs so they have to be copied specially:
        auto dst = (Val*)newBlock->data().begin();
        for (Val &src : vals)
            *dst++ = src;
    } else {
        newBlock->fill(data);
    }
    ::memset(&newBlock->data()[data.size()], 0, newDataSize - data.size());
    return newBlock;
}



Block* Heap::firstBlock() {
    return (Block*)(_base + sizeof(Header));
}

Block* Heap::nextBlock(Block *b) {
    b = b->nextBlock();
    return (byte*)b < _cur ? b : nullptr;
}


void Heap::visitRoots(BlockVisitor const& visitor) {
    auto &header = this->header();
    if (header.root != nullpos)
        if (!visitor(*(Block*)at(header.root))) return;
    if (header.symbols != nullpos)
        if (!visitor(*(Block*)at(header.symbols))) return;
    for (Object *refp : _externalRootObjs) {
        if (auto block = refp->block())
            if (!visitor(*block)) return;
    }
    for (Value *refp : _externalRootVals) {
        if (auto block = refp->block())
            if (!visitor(*block)) return;
    }
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
        assert(contains(b) && (void*)b >= &header()+1);
        if (!b->isVisited()) {
            b->setVisited();
            if (!visitor(*b))
                return false;
            if (TypeIs(b->type(), TypeSet::Container) && b->dataSize() > 0)
                stack.push_back(b);
        }
        return true;
    };

    visitRoots([&](Block const& block) {
        return processBlock(const_cast<Block*>(&block));
    });

    while (!stack.empty()) {
        Block *b = stack.front();
        stack.pop_front();
        for (Val const& val : b->vals()) {
            if (Block *block = val.block(); block && !processBlock(block))
                return;
        }
    }
}


void Heap::dump(std::ostream &out) {
    auto writeAddr = [&](const void *addr) -> std::ostream& {
        return out << addr << std::showpos << std::setw(8) << intpos(pos(addr))
        << std::noshowpos << " | ";
    };

    // First walk the object graph to set the "visited" flag on live objects:
    visitBlocks([&](Block const&) { return true; });

    Block const* rootBlock = nullptr;
    Block const* symBlock = nullptr;
    auto &header = this->header();
    if (header.root != nullpos)
        rootBlock = (Block*)at(header.root);
    if (header.symbols != nullpos)
        symBlock = (Block*)at(header.symbols);
    std::unordered_set<Block const*> externalRoots;
    visitRoots([&](Block const& block) {
        externalRoots.insert(&block);
        return true;
    });


    writeAddr(_base) << "--- HEAP BASE ---\n";
    visitAll([&](Block const& block) {
        writeAddr(&block);
        out << std::setw(4) << block.dataSize() << " bytes : ";
        Value val(&block);
        switch (val.type()) {
            case Type::Array:   out << "Array[" << val.as<Array>().size() << "]"; break;
            case Type::Vector:  out << "Vector[" << val.as<Vector>().size()
                                    << " / " << val.as<Vector>().capacity() << "]"; break;
            case Type::Dict:    out << "Dict[" << val.as<Dict>().size() << " / "
                                    << val.as<Dict>().capacity() << "]"; break;
            default:            out << val; break;
        }
        if (&block == rootBlock)
            out << "  <==ROOT";
        if (&block == symBlock)
            out << "  <--SymbolTable";
        if (externalRoots.find(&block) != externalRoots.end())
            out << "  <--root";
        if (!block.isVisited())
            out << "  🞮";
        out << std::endl;
        return true;
    });
    writeAddr(_cur) << "--- cur ---\n";
    writeAddr(_end) << "--- HEAP END ---\n";
}


void Heap::visit(ObjectVisitor visitor) {
    visitBlocks([&](Block const& block) { return visitor(Object(&block)); });
}


template <class T> static inline void _registerRoot(Heap const* self, std::vector<T*> &roots, T *ref) {
    assert(ref->block() == nullptr || self->contains(ref->block()));
    roots.push_back(ref);
}
template <class T> static inline void _unregisterRoot(std::vector<T*> &roots, T *ref) {
    auto i = std::find(roots.rbegin(), roots.rend(), ref);
    assert(i != roots.rend());
    roots.erase(std::prev(i.base()));
}

void Heap::registerExternalRoot(Value *ref) const    {_registerRoot(this, _externalRootVals, ref);}
void Heap::unregisterExternalRoot(Value* ref) const  {_unregisterRoot(_externalRootVals, ref);}
void Heap::registerExternalRoot(Object *ref) const   {_registerRoot(this, _externalRootObjs, ref);}
void Heap::unregisterExternalRoot(Object* ref) const {_unregisterRoot(_externalRootObjs, ref);}


#pragma mark - CREATING OBJECTS:


template <ObjectClass T>
static Maybe<T> newObject(slice<typename T::Item> items, size_t capacity, Heap &heap) {
    Block *block = heap.allocBlock(heapsize(capacity * sizeof(typename T::Item)), T::Type);
    if (!block)
        return nullptr;
    if constexpr (std::is_same<typename T::Item, Val>::value)
        block->fill(items);
    else
        block->fill(slice_cast<byte>(items));
    Object obj(block);
    return (Maybe<T>&)obj;
}

template <ObjectClass T>
static Maybe<T> newObject(slice<typename T::Item> items, Heap &heap) {
    return newObject<T>(items, items.size(), heap);
}

template <ObjectClass T>
static Maybe<T> newObject(size_t capacity, Heap &heap) {
    return newObject<T>({}, capacity, heap);
}


Maybe<BigInt> newBigInt(int64_t i, Heap &heap) {
    //TODO: Use fewer bytes if possible
    Block *block = heap.allocBlock(sizeof(int64_t), Type::BigInt);
    if (!block)
        return nullptr;
    *(int64_t*)block->dataPtr() = i;
    Object obj(block);
    return (Maybe<BigInt>&)obj;
}

Value newInt(int64_t i, Heap &heap) {
    if (i >= Int::Min && i <= Int::Max)
        return Int(int(i));
    else
        return newBigInt(i, heap);
}


template <typename F>
static Maybe<Float> _newFloat(F f, Heap &heap) {
    Block *block = heap.allocBlock(sizeof(F), Type::Float);
    if (!block)
        return nullptr;
    *(F*)block->dataPtr() = f;
    Object obj(block);
    return (Maybe<Float>&)obj;
}

Maybe<Float> newFloat(float f, Heap &heap)  {return _newFloat(f, heap);}

Maybe<Float> newFloat(double d, Heap &heap) {
    if (float f = float(d); f == d)
        return _newFloat(f, heap);
    else
        return _newFloat(d, heap);
}

Value newNumber(double d, Heap &heap) {
    if (auto i = int64_t(d); i == d)
        return newInt(i, heap);
    else
        return newFloat(d, heap);
}

Maybe<String> newString(std::string_view str, Heap &heap) {
    return newString(str.data(), str.size(), heap);
}
Maybe<String> newString(const char *str, size_t length, Heap &heap) {
    return newObject<String>({(char*)str, length}, heap);
}


Value Symbol::create(std::string_view str, Heap &heap) {
    return newObject<Symbol>({(char*)str.data(), str.size()}, heap);
}
Maybe<Symbol> newSymbol(std::string_view str, Heap &heap) {
    return heap.symbolTable().create(str);
}
Maybe<Symbol> newSymbol(const char *str, size_t length, Heap &heap) {
    return newSymbol(std::string_view(str, length), heap);
}


Maybe<Blob> newBlob(size_t capacity, Heap &heap) {
    return newObject<Blob>(capacity, heap);
}
Maybe<Blob> newBlob(const void *data, size_t size, Heap &heap) {
    return newObject<Blob>({(byte*)data, size}, heap);
}


Maybe<Array> newArray(heapsize count, Heap &heap) {
    return newObject<Array>(count, heap);
}
Maybe<Array> newArray(heapsize count, Value initialValue, Heap &heap) {
    auto ma = newObject<Array>(count, heap);
    if_let(array, ma) {
        for (Val& item : array.items())
            item = initialValue;
    }
    return ma;
}
Maybe<Array> newArray(slice<Val> vals, size_t capacity, Heap &heap) {
    return newObject<Array>(vals, capacity, heap);
}


Maybe<Vector> newVector(heapsize capacity, Heap &heap) {
    auto v = newObject<Vector>(capacity + 1, heap);
    if_let(vv, v) {vv.clear();}
    return v;
}
Maybe<Vector> newVector(slice<Val> vals, size_t capacity, Heap &heap) {
    return newObject<Vector>(vals, capacity + 1, heap);
}


Maybe<Dict> newDict(heapsize capacity, Heap &heap) {
    return newObject<Dict>(capacity, heap);
}

}
