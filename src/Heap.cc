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
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace snej::smol {

static constexpr uint32_t kMagic = 0xA189933A;

struct Heap::Header {
    uint32_t magic;   // Must equal kMagic
    heappos  root;    // Pointer to root object
    heappos  symbols; // Pointer to symbol table
    Type     rootType; // Type of root object
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
    assert(this != maybeCurrent());
    if (_malloced) free(_base);
    free(_error);
    unregistr();
}


Heap::Heap()                                    :_base(nullptr), _end(nullptr), _cur(nullptr) { }
Heap::Heap(void *base, size_t cap) noexcept     :Heap(base, cap, false) {reset();}
Heap::Heap(size_t cap)                          :Heap(::malloc(cap), cap, true) {reset();}
Heap::Heap(const char *error)                   {_error = strdup(error);}

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
    // The symbolTable and root stay with the heap.
    // _allocFailureHandle and _externalRoots are not swapped, they belong to the Heap itself.
}

bool Heap::empty() const                {return used() <= sizeof(Header);}

Heap const* Heap::enter() const         {auto prev = sCurHeap; sCurHeap = this; return prev;}
void Heap::exit(Heap const* next) const {assert(sCurHeap == this); sCurHeap = (Heap*)next;}
Heap* Heap::maybeCurrent()              {return (Heap*)sCurHeap;}
Heap* Heap::current()                   {assert(sCurHeap); return (Heap*)sCurHeap;}


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

heappos Heap::valueToPos(Value obj) const {
    return obj.isObject() ? pos(obj.block()) : nullpos;
}


#pragma mark - ROOTS & SYMBOL TABLE:


Maybe<Object> Heap::root() const {
    Header const& hdr = header();
    if (hdr.root == nullpos)
        return {};
    return Object((Block*)at(hdr.root), hdr.rootType);
}

void Heap::setRoot(Maybe<Object> root) {
    Header& hdr = header();
    if_let(rootObj, root) {
        hdr.root = valueToPos(rootObj);
        hdr.rootType = rootObj.type();
    } else {
        hdr.root = nullpos;
        hdr.rootType = Type::Null;
    }
}

Value Heap::symbolTableArray() const {
    Header const& hdr = header();
    if (hdr.symbols == nullpos)
        return nullvalue;
    return Value((Block*)at(hdr.symbols), Type::Array);
}

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


/// The hint byte placed before each block header when `_iterable` is true.
struct Heap::BlockHint {
    uint8_t headerSize  :3;
    Type    type        :4;
    bool    visited     :1;
};


Block* Heap::allocBlock(heapsize size, Type type) {
    BlockHeader headerBuf;
    slice<byte> header = headerBuf.init(size);
    if (_iterable) {
        // Prepend a hint byte giving the header size:
        auto start = header.begin() - 1;
        auto hint = (BlockHint*)start;
        hint->headerSize = header.size();
        hint->type = type;
        hint->visited = false;
        header = slice<byte>{start, header.size() + 1};
    }

    if (auto addr = (byte*)rawAlloc(header.sizeInBytes() + std::max(size, Block::MinSize))) {
        header.memcpyTo(addr);
        return (Block*)(addr + header.sizeInBytes());
    } else {
        return nullptr;
    }
}


Block* Heap::allocBlock(heapsize size, Type type, slice<byte> contents) {
    Block *block = allocBlock(size, type);
    if (block)
        block->fill(contents);
    return block;
}


Block* Heap::reallocBlock(Block* block, Type type, heapsize newDataSize) {
    auto data = block->data();
    auto oldDataSize = data.size();
    if (newDataSize == oldDataSize)
        return block;
    if (_iterable)
        assert(hint(block).type == type);

    Handle<Value> val{Value(block, type)};
    auto newBlock = allocBlock(newDataSize, type);
    if (!newBlock)
        return nullptr;
    block = val.block();    // in case GC occurred
    data = block->data();
    if (newDataSize < oldDataSize)
        data.resize(newDataSize);

    if (TypeIs(type, TypeSet::Container)) {
        // Vals are relative ptrs so they have to be copied specially:
        auto dst = (Val*)newBlock->data().begin();
        for (Val &src : slice_cast<Val>(data))
            *dst++ = src;
    } else {
        newBlock->fill(data);
    }
    if (newDataSize > oldDataSize)
        ::memset(&newBlock->data()[data.size()], 0, newDataSize - data.size());
    return newBlock;
}


#pragma mark - ITERATION / VISITING:


Heap::BlockHint& Heap::hint(Block const* b) const {
    assert(_iterable);
    assert(contains(b) && (void*)b >= &header()+1);
    return *(BlockHint*)((uint8_t*)b - b->header().headerSize() - 1);
}

Type Heap::typeHint(Block const* b) const   {return hint(b).type;}



Block const* Heap::skipBlockHeader(const void *addr) const {
    static_assert(sizeof(BlockHint) == 1);
    assert(_iterable);
    if (addr == _cur)
        return nullptr;
    assert(addr < _cur);
    auto hint = (BlockHint* const)addr;
    return (Block const*)( (byte*)addr + sizeof(BlockHint) + hint->headerSize );
}

Block const* Heap::firstBlock() const {
    return skipBlockHeader(_base + sizeof(Header));
}

Block const* Heap::nextBlock(Block const* b) const {
    return skipBlockHeader(b->end());
}

slice<Val> Heap::blockVals(Block const* b) const {
    if (TypeIs(typeHint(b), TypeSet::Container))
        return slice_cast<Val>(b->data());
    else
        return {};
}

void Heap::visitRoots(BlockVisitor const& visitor) {
    preventGCDuring([&]{
        auto &header = this->header();
        if (header.root != nullpos)
            if (!visitor(*(Block*)at(header.root), header.rootType)) return;
        if (header.symbols != nullpos)
            if (!visitor(*(Block*)at(header.symbols), Type::Array)) return;
        for (Object *refp : _externalRootObjs) {
            if (auto block = refp->block())
                if (!visitor(*block, typeHint(block))) return;
        }
        for (Value *refp : _externalRootVals) {
            if (auto block = refp->block())
                if (!visitor(*block, typeHint(block))) return;
        }
    });
}

void Heap::visitBlocks(BlockVisitor visitor) {
    preventGCDuring([&]{
        for (auto b = firstBlock(); b; b = nextBlock(b))
            hint(b).visited = false;
        
        std::deque<Block*> stack;
        
        auto processBlock = [&](Block *b) -> bool {
            BlockHint &h = hint(b);
            if (!h.visited) {
                h.visited = true;
                if (!visitor(*b, hint(b).type))
                    return false;
                if (!blockVals(b).empty())
                    stack.push_back(b);
            }
            return true;
        };
        
        visitRoots([&](Block const& block, Type) {
            return processBlock(const_cast<Block*>(&block));
        });
        
        while (!stack.empty()) {
            Block *b = stack.front();
            stack.pop_front();
            for (Val const& val : blockVals(b)) {
                if (Block *block = val.block(); block && !processBlock(block))
                    return;
            }
        }
    });
}


void Heap::visit(ObjectVisitor visitor) {
    visitBlocks([&](Block const& block, Type type) { return visitor(Object(&block, type)); });
}


#pragma mark - HEAP VALIDATE & DUMP:


bool Heap::fail(const char *fmt, ...) const {
    char* cstr = nullptr;
    va_list args;
    va_start(args, fmt);
    vasprintf(&cstr, fmt, args);
    va_end(args);
    if (cstr)
        _error = cstr;
    else
        _error = strdup(fmt);
    std::cerr << "INVALID HEAP: " << _error << std::endl;
    return false;
}


Block const* Heap::_nextBlock(Block const* b) const {
    assert(_iterable);
    const void *end = b->end();
    if (end == _cur)
        return (Block const*)end;
    else if (!contains(end))
        return nullptr;
    auto hint = (BlockHint* const)end;
    b = (Block*)offsetBy(end, sizeof(BlockHint) + hint->headerSize);
    if (!contains(b))
        return nullptr;
    return b;
}


struct heapRef {
    const void* src;
    heappos dst;
    Type type;

    friend bool operator<(heapRef const& a, heapRef const& b) {return a.dst < b.dst;}
};


bool Heap::validate() const {
    if (capacity() < sizeof(Header) || capacity() < used())
        return fail("Invalid size or used");
    auto hdr = header();
    if (hdr.magic != kMagic)
        return fail("wrong magic number %08X; should be %08X", hdr.magic, kMagic);
    std::set<heapRef> forwardRefs, backwardRefs;
    if (hdr.root != nullpos) {
        if (hdr.root < sizeof(Header) || hdr.root >= used())
            return fail("invalid root offset %d", hdr.root);
        forwardRefs.insert(heapRef{&hdr.root, hdr.root, hdr.rootType});
    }
    if (hdr.symbols != nullpos) {
        if (hdr.symbols < sizeof(Header) || hdr.symbols >= used())
            return fail("invalid symbol table offset %d", hdr.symbols);
        forwardRefs.insert({&hdr.symbols, hdr.symbols, Type::Array});
    }

    if (!_iterable) {
        std::cerr << "WARNING: Unable to run full validate() on non-iterable Heap!\n";
        return true; // Cannot walk heap if _iterable is not set
    }

    unsigned maxForwards = 0, numBacks = 0;
    const heapRef kEndPos{nullptr, _pos(_end), Type::Null};
    heapRef nextFwdRef = forwardRefs.empty() ? kEndPos : *forwardRefs.begin();
    Block const* first = firstBlock(), *last = first;
    if (first) {
        // Examine each block:
        Block const* next;
        for (auto block = first; block < (void*)_cur; last = block, block = next) {
            //std::cout << "_validate: Block " << (void*)block << "  size " << block->sizeInBytes() << std::endl;
            if (block->isForwarded())
                return fail("block %p is forwarded", block);
            // Validate size:
            auto blockEnd = block->end();
            next = _nextBlock(block);
            if (!next)
                return fail("block %p--%p overflows end of heap %p", block, blockEnd, _end);
            if (next != (void*)_cur) {
                if (next->withHeader().first.begin() != (byte*)blockEnd + sizeof(BlockHint))
                    fail("Hint for block %p seems to be wrong", next);
            }

            Type blockType = typeHint(block);
            if (IsContainer(blockType) && block->sizeInBytes() % 4 != 0)
                return fail("block %p is container (type %d) but has uneven size %d",
                            block, blockType, block->sizeInBytes());

            // See if this block resolves a forward ref:
            if (auto blockPos = _pos(block); blockPos >= nextFwdRef.dst) {
                if (blockPos > nextFwdRef.dst)
                    return fail("pointer at %p points to %p, inside block %p--%p",
                                nextFwdRef.src, _at(nextFwdRef.dst), last, block);
                if (blockType != nextFwdRef.type)
                    return fail("Val at %p's type %d doesn't match dst block %p's type %d",
                                nextFwdRef.src, nextFwdRef.type, block, blockType);
                forwardRefs.erase(forwardRefs.begin());
                nextFwdRef = forwardRefs.empty() ? kEndPos : *forwardRefs.begin();
            }

            // Scan the pointers in the block:
            if (TypeIs(blockType, TypeSet::Container)) {
                for (Val const& val : slice_cast<Val>(block->data())) {
                    if (auto ptr = val.block()) {
                        heapRef ptrPos = {&val, _pos(ptr), val.type()};
                        if (ptr < block) {
                            // Backward ref:
                            if (ptr < first)
                                return fail("Val at %p points to %p, before the heap",
                                            ptrPos.src, ptr);
                            backwardRefs.insert(ptrPos);
                            numBacks++;
                        } else if (ptr > next) {
                            // Forward ref:
                            if (ptr > (void*)_cur)
                                return fail("pointer at %p points to %p, after the heap",
                                            ptrPos.src, ptr);
                            forwardRefs.insert(ptrPos);
                            if (ptrPos.dst < nextFwdRef.dst)
                                nextFwdRef = ptrPos;
                            maxForwards = std::max(maxForwards, unsigned(forwardRefs.size()));
                        } else if (ptr > block && ptr < next) {
                            return fail("pointer at %p points to %p, inside the block %p--%p it belongs to",
                                        ptrPos.src, ptr, block, blockEnd);
                        }
                    }
                }
            }
        }

        //std::cout << "There are " << backwardRefs.size() << " unique backward refs out of "
        //          << numBacks << " total.\n";
        if (!backwardRefs.empty()) {
            // Iterate the blocks again to resolve the backward refs. For efficiency, do this by
            // copying the refs in order into a vector; then step through the vector as we find them.
            std::vector<heapRef> backs(backwardRefs.begin(), backwardRefs.end());
            backwardRefs.clear();
            auto nextBack = backs.begin();
            for (auto block = first; block < (void*)_cur; block = nextBlock(block)) {
                if (heappos blockPos = _pos(block); blockPos == nextBack->dst) {
                    if (typeHint(block) != nextBack->type)
                        return fail("Val at %p's type doesn't match its block %p's type %p",
                                    nextBack->src, nextBack->type, block, typeHint(block));
                    if (++nextBack == backs.end())
                        break;
                } else if (blockPos > nextBack->dst) {
                    break; // and fail
                }
            }
            if (nextBack != backs.end())
                return fail("invalid pointer at %p to %p", nextBack->src, _at(nextBack->dst));
        }
    }

    if (!forwardRefs.empty())
        return fail("invalid pointer at %p to %p",
                    forwardRefs.begin()->src, _at( forwardRefs.begin()->dst));
    //std::cout << "Validation complete. There were max " << maxForwards << " forward refs being tracked.\n";
    return true;
}


void Heap::dump() {dump(std::cout);}

void Heap::dump(std::ostream &out) {
    if (!_iterable) {
        out << "Cannot dump non-iterable Heap.\n";
        return;
    }

    auto writeAddr = [&](const void *addr) -> std::ostream& {
        return out << addr << std::showpos << std::setw(8) << intpos(pos(addr))
        << std::noshowpos << " | ";
    };

    // First walk the object graph to set the "visited" flag on live objects:
    visitBlocks([&](Block const&, Type) { return true; });

    Block const* rootBlock = nullptr;
    Block const* symBlock = nullptr;
    auto &header = this->header();
    if (header.root != nullpos)
        rootBlock = (Block*)at(header.root);
    if (header.symbols != nullpos)
        symBlock = (Block*)at(header.symbols);
    std::unordered_set<Block const*> externalRoots;
    visitRoots([&](Block const& block, Type) {
        externalRoots.insert(&block);
        return true;
    });

    unsigned blocks = 0;
    unsigned fwdLinks = 0, backLinks = 0;
    intpos biggestPtr = 0;
    heappos biggestPtrAt = nullpos;

    writeAddr(_base) << "--- HEAP BASE ---\n";
    bool ok = visitAll([&](Block const& block, Type type) {
        writeAddr(&block);
//        if (const char *err = block.validate()) {
//            out << "**** INVALID BLOCK!!! " << err << " -- header = 0x" << std::hex << *(uint16_t*)&block << std::dec << std::endl;
//            return false;
//        }
        out << std::setw(4) << block.sizeInBytes() << " bytes : ";
        Value val(&block, type);
        switch (type) {
            case Type::String: {
                std::string_view str = val.as<String>().str();
                out << "“" << str.substr(0, std::min(str.size(),size_t(50)))
                    << (str.size() <= 50 ? "”" : "……");
                break;
            }
            case Type::Array:   out << "Array[" << val.as<Array>().itemCount() << " / "
                                    << val.as<Array>().size() << "]"; break;
#ifdef VECTOR
            case Type::Vector:  out << "Vector[" << val.as<Vector>().size()
                                    << " / " << val.as<Vector>().capacity() << "]"; break;
#endif
            case Type::Dict:    out << "Dict[" << val.as<Dict>().size() << " / "
                                    << val.as<Dict>().capacity() << "]"; break;
            default:            out << val; break;
        }

        ++blocks;

        for (auto& val : blockVals(&block)) {
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
        if (!hint(&block).visited)
            out << "  🞮";

        out << std::endl;
        return true;
    });
    if (!ok)
        return; // bad heap
    writeAddr(_cur) << "--- cur ---\n";
    writeAddr(_end) << "--- HEAP END ---\n" << blocks << " blocks:";

    out << fwdLinks << " forward pointers, " << backLinks << " backward pointers.\n";
    out << "Farthest pointer is " << biggestPtr << " bytes, at " << uintpos(biggestPtrAt) << ".\n";
}


void Heap::dumpBlockSizes(std::ostream &out) {
    unsigned blocks = 0;
    unsigned byType[16] = {};
    unsigned sizeByType[16] = {};
    std::map<heapsize,unsigned> blockSizes;

    visitAll([&](Block const& block, Type type) {
        ++blocks;
        ++byType[int(type)];
        sizeByType[int(type)] += block.sizeWithHeader() + _iterable;
        blockSizes[block.sizeInBytes()] += 1;
        return true;
    });

    for (int t = 0; t < 16; t++) {
        if (byType[t])
            out << "  " << byType[t] << " " << TypeName(Type(t)) << "s (" << sizeByType[t] << " b)";
    }
    out << "\n";

    unsigned maxCount = 0, maxTotalSize = 0;
    for (auto [size, count] : blockSizes) {
        maxCount = std::max(maxCount, count);
        maxTotalSize = std::max(maxTotalSize, size * count);
    }
    for (auto [size, count] : blockSizes) {
        auto width = (120 * count + maxCount/2) / maxCount;
        out << std::setw(4) << size << " bytes : " << std::setw(4) << count << " blocks | "
            << std::string(width, '*') << std::endl;
        auto totalSize = size * count;
        width = (120 * totalSize + maxTotalSize/2) / maxTotalSize;
        out << "           " << std::setw(7) << totalSize << " bytes | "
            << std::string(width, '=');
        auto totalSizePlusHeaders = totalSize + count * (BlockHeader::sizeForDataSize(size) + _iterable);
        auto widthWithHeaders = (120 * totalSizePlusHeaders + maxTotalSize/2) / maxTotalSize;
        out << std::string(widthWithHeaders - width, '-') << std::endl;
    }
}


void Heap::dumpStrings(std::ostream &out) {
    std::unordered_map<std::string_view,unsigned> strings;

    visitAll([&](Block const& block, Type type) {
        Value val(&block, type);
        switch (type) {
            case Type::String: {
                std::string_view str = val.as<String>().str();
                auto [i, isNew] = strings.insert({str, 0});
                i->second++;
                break;
            }
            default:
                break;
        }
        return true;
    });

    size_t stringSize = 0, wasted = 0;
    for (auto [str,count] : strings) {
        if (count > 1 ) out << "\t'" << str << "' x " << count << std::endl;
        stringSize += str.size() + 2;
        wasted += (count - 1) * str.size();
    }
    out << strings.size() << " unique strings, " << stringSize << " bytes; dups waste " << wasted << " bytes\n";
}


#pragma mark - CREATING OBJECTS:


template <ObjectClass T>
static Maybe<T> newObject(slice<typename T::Item> items, size_t capacity, Heap &heap) {
    Block *block = heap.allocBlock(heapsize(capacity * sizeof(typename T::Item)), T::Type);
    if (!block)
        return nullptr;
    Object obj(block, T::Type);
    if constexpr (std::is_same<typename T::Item, Val>::value)
        obj.fill(items);
    else
        block->fill(slice_cast<byte>(items));
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


#ifdef BIGINT
Maybe<BigInt> newBigInt(int64_t i, Heap &heap) {
    //TODO: Use fewer bytes if possible
    Block *block = heap.allocBlock(sizeof(int64_t), Type::BigInt);
    if (!block)
        return nullptr;
    *(int64_t*)block = i;
    Object obj(block);
    return (Maybe<BigInt>&)obj;
}
#endif

Value newInt(int64_t i, Heap &heap) {
    if (i >= Int::Min && i <= Int::Max)
        return Int(int(i));
    else {
#ifdef BIGINT
        return newBigInt(i, heap);
#else
        return newFloat(double(i), heap);
#endif
    }
}


template <typename F>
static Maybe<Float> _newFloat(F f, Heap &heap) {
    Block *block = heap.allocBlock(sizeof(F), Type::Float);
    if (!block)
        return nullptr;
    *(F*)block = f;
    Object obj(block, Type::Float);
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


#ifdef VECTOR
Maybe<Vector> newVector(heapsize capacity, Heap &heap) {
    auto v = newObject<Vector>(capacity + 1, heap);
    if_let(vv, v) {vv.clear();}
    return v;
}
Maybe<Vector> newVector(slice<Val> vals, size_t capacity, Heap &heap) {
    return newObject<Vector>(vals, capacity + 1, heap);
}
#endif


Maybe<Dict> newDict(heapsize capacity, Heap &heap) {
    return newObject<Dict>(capacity, heap);
}

}
