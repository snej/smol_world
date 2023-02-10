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
#include <set>
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
Heap::Heap(const char *error)                   {_error = error;}

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

Heap const* Heap::enter() const     {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const  {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::current()               {return (Heap*)sCurHeap;}


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
    if (contents.size() < sizeof(Header) || contents.size() > capacity)
        return Heap("invalid size or capacity");
    Heap heap(contents.begin(), capacity, false);
    heap._cur = contents.end();
    heap._mayHaveSymbols = true;

    auto header = heap.header();
    if (header.magic != kMagic)
        return Heap("wrong magic number");
    if (header.root != nullpos) {
        if (header.root < sizeof(Header) || header.root >= heap.used())
            return Heap("bad root offset");
    }
    if (header.symbols != nullpos) {
        if (header.symbols < sizeof(Header) || header.symbols >= heap.used())
            return Heap("bad symbol table offset");
    }
    return heap;
}

bool Heap::validPos(heappos pos) const          {return pos >= sizeof(Header) && pos < used();}

Value Heap::posToValue(heappos pos) const {
    if (pos == nullpos)
        return nullptr;
    return Value((Block*)at(pos));
}

heappos Heap::valueToPos(Value obj) const {
    return obj.isObject() ? pos(obj.block()) : nullpos;
}


#pragma mark - ROOTS & SYMBOL TABLE:


Maybe<Object> Heap::root() const                {return posToValue(header().root).maybeAs<Object>();}
void Heap::setRoot(Maybe<Object> root)          {header().root = valueToPos(root);}
Value Heap::symbolTableArray() const            {return posToValue(header().symbols);}
void Heap::setSymbolTableArray(Value v)         {header().symbols = valueToPos(v);}


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


SymbolTable& Heap::symbolTable() {
    if (!_symbolTable) {
        if_let(symbols, symbolTableArray().maybeAs<Array>()) {
            _symbolTable = std::make_unique<SymbolTable>(this, symbols);
        } else if (_mayHaveSymbols) {
            _symbolTable = SymbolTable::rebuild(this);
        } else {
            _symbolTable = SymbolTable::create(this);
            _mayHaveSymbols = true;
        }
        //FIXME: What if this fails?
    }
    return *_symbolTable;
}


void Heap::dropSymbolTable() {
    _symbolTable.reset();
    header().symbols = nullpos;
}


#pragma mark - ALLOCATION:


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
                      << avail << " available";
            if (_cannotGC)
                std::cerr << ", CANNOT GC!";
            std::cerr << " -- invoking failure handler **\n";
            if (!_allocFailureHandler(this, size, !_cannotGC))
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


Block* Heap::reallocBlock(Block* block, heapsize newDataSize) {
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


#pragma mark - ITERATION / VISITING:


Block const* Heap::firstBlock() const{
    auto b = (Block const*)(_base + sizeof(Header));
    return (byte*)b < _cur ? b : nullptr;
}

Block const* Heap::nextBlock(Block const* b) const {
    b = b->nextBlock();
    return (byte*)b < _cur ? b : nullptr;
}


void Heap::visitRoots(BlockVisitor const& visitor) {
    preventGCDuring([&]{
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
    });
}


void Heap::visitBlocks(BlockVisitor visitor) {
    preventGCDuring([&]{
        for (auto b = firstBlock(); b; b = nextBlock(b))
            const_cast<Block*>(b)->clearVisited();
        
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
    });
}


void Heap::visit(ObjectVisitor visitor) {
    visitBlocks([&](Block const& block) { return visitor(Object(&block)); });
}


#pragma mark - HEAP VALIDATE & DUMP:


const char* Heap::_validate() const {
    if (capacity() < sizeof(Header) || capacity() < used())
        return "Invalid size or used";
    auto hdr = header();
    if (hdr.magic != kMagic)
        return "wrong magic number";
    std::set<heappos> forwardRefs, backwardRefs;
    unsigned maxForwards = 0, numBacks = 0;
    if (hdr.root != nullpos) {
        if (hdr.root < sizeof(Header) || hdr.root >= used())
            return "invalid root offset";
        forwardRefs.insert(hdr.root);
    }
    if (hdr.symbols != nullpos) {
        if (hdr.symbols < sizeof(Header) || hdr.symbols >= used())
            return "invalid symbol table offset";
        forwardRefs.insert(hdr.symbols);
    }

    heappos nextFwdRef = forwardRefs.empty() ? _pos(_end) : *forwardRefs.begin();
    Block const* first = firstBlock();
    if (first) {
        // Examine each block:
        Block const* next;
        for (auto block = first; block < (void*)_cur; block = next) {
            // Validate block header:
            if (const char* error = block->validate(); error)
                return error;
            // Validate size:
            next = block->nextBlock();
            if (next > (void*)_cur)
                return "block overflows end of heap";

            // See if this block resolves a forward ref:
            if (auto blockPos = _pos(block); blockPos >= nextFwdRef) {
                if (blockPos > nextFwdRef)
                    return "there is an invalid (forward) pointer";
                forwardRefs.erase(forwardRefs.begin());
                nextFwdRef = forwardRefs.empty() ? _pos(_end) : *forwardRefs.begin();
            }

            // Scan the pointers in the block:
            for (Val const& val : block->vals()) {
                if (auto ptr = val.block()) {
                    if (ptr < block) {
                        // Backward ref:
                        if (ptr < first)
                            return "a pointer points outside the heap";
                        backwardRefs.insert(_pos(ptr));
                        numBacks++;
                    } else if (ptr > next) {
                        // Forward ref:
                        if (ptr > (void*)_cur)
                            return "a pointer points outside the heap";
                        heappos ptrPos = _pos(ptr);
                        forwardRefs.insert(ptrPos);
                        nextFwdRef = std::min(nextFwdRef, ptrPos);
                        maxForwards = std::max(maxForwards, unsigned(forwardRefs.size()));
                    } else if (ptr > block && ptr < next) {
                        return "a pointer points inside the object it belongs to";
                    }
                }
            }
        }

        //std::cout << "There are " << backwardRefs.size() << " unique backward refs out of "
        //          << numBacks << " total.\n";
        if (!backwardRefs.empty()) {
            // Iterate the blocks again to resolve the backward refs. For efficiency, do this by
            // copying the refs in order into a vector; then step through the vector as we find them.
            std::vector<heappos> backs(backwardRefs.begin(), backwardRefs.end());
            backwardRefs.clear();
            auto nextBack = backs.begin();
            for (auto block = first; block < (void*)_cur; block = block->nextBlock()) {
                if (heappos blockPos = _pos(block); blockPos == *nextBack) {
                    if (++nextBack == backs.end())
                        break;
                } else if (blockPos > *nextBack) {
                    return "there is a bad (backward) pointer within the heap";
                }
            }
            if (nextBack != backs.end()) return "there are bad (backward) pointers within the heap";
        }
    }

    if (!forwardRefs.empty()) return "there are bad (forward) pointers within the heap";
    //std::cout << "Validation complete. There were max " << maxForwards << " forward refs being tracked.\n";
    return nullptr;
}

bool Heap::validate() const {
    if (const char* error = _validate()) {
        _error = error;
        std::cerr << "INVALID HEAP: " << error << std::endl;
        return false;
    }
    return true;
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

    unsigned blocks = 0;
    unsigned byType[16] = {};
    unsigned sizeByType[16] = {};
    unsigned fwdLinks = 0, backLinks = 0;
    intpos biggestPtr = 0;
    heappos biggestPtrAt = nullpos;

    writeAddr(_base) << "--- HEAP BASE ---\n";
    visitAll([&](Block const& block) {
        writeAddr(&block);
        out << std::setw(4) << block.dataSize() << " bytes : ";
        Value val(&block);
        switch (val.type()) {
            case Type::String: {
                std::string_view str = val.as<String>().str();
                out << "“" << str.substr(0, std::min(str.size(),size_t(50)))
                    << (str.size() <= 50 ? "”" : "……");
                break;
            }
            case Type::Array:   out << "Array[" << val.as<Array>().size() << "]"; break;
            case Type::Vector:  out << "Vector[" << val.as<Vector>().size()
                                    << " / " << val.as<Vector>().capacity() << "]"; break;
            case Type::Dict:    out << "Dict[" << val.as<Dict>().size() << " / "
                                    << val.as<Dict>().capacity() << "]"; break;
            default:            out << val; break;
        }

        ++blocks;
        ++byType[int(val.type())];
        sizeByType[int(val.type())] += block.blockSize();

        for (auto& val : block.vals()) {
            if (auto dstBlock = val.block(); dstBlock) {
                if (dstBlock < &block)
                    backLinks++;
                else
                    fwdLinks++;
                intpos distance = intpos((byte*)dstBlock - (byte*)&block);
                if (abs(distance) > abs(biggestPtr)) {
                    biggestPtr = distance;
                    biggestPtrAt = pos(&block);
                }
            }
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
    writeAddr(_end) << "--- HEAP END ---\n" << blocks << " blocks:";
    for (int t = 0; t < 16; t++) {
        if (byType[t])
            out << "  " << byType[t] << " " << TypeName(Type(t)) << "s (" << sizeByType[t] << " b)";
    }
    out << "\n" << fwdLinks << " forward pointers, " << backLinks << " backward pointers.\n";
    out << "Farthest pointer is " << biggestPtr << " bytes, at " << uintpos(biggestPtrAt) << ".\n";
}


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
    return newObject<String>({(char*)str.data(), str.size()}, heap);
}


Maybe<Symbol> newSymbol(std::string_view str, Heap &heap) {
    return heap.symbolTable().create(str);
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
