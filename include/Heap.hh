//
// Heap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.hh"
#include "Collections.hh"
#include "function_ref.hh"
#include "slice.hh"
#include <compare>
#include <vector>

namespace snej::smol {

/// A container for dynamic allocation.
/// Pointers within a Heap are 32-bit values, offsets from the heap's base address.
/// Allocation uses a simple bump (arena) allocator.
class Heap {
public:
    static constexpr size_t  MaxSize = 1 << 31;
    static const size_t Overhead;

    /// Constructs a new empty Heap starting at address `base` and capacity `size`.
    Heap(void *base, size_t capacity) noexcept;

    /// Constructs a new empty Heap with space allocated by malloc.
    explicit Heap(size_t capacity);

    Heap(Heap&&) noexcept;
    Heap& operator=(Heap&&) noexcept;
    ~Heap();

    /// Constructs a Heap from already-existing heap data. Throws if the data is not valid.
    static Heap existing(slice<byte> contents, size_t capacity);

    /// Carefully checks a Heap for invalid metadata. Returns nullptr on success, else an error.
    bool validate() const;

    /// If the heap is invalid, returns an error message, else nullptr.
    const char* invalid() const         {return _error;}

    const void*  base() const           {return _base;}         ///< Address of start of heap.
    const size_t capacity() const       {return _end - _base;}  ///< Maximum size it can grow to
    const size_t used() const           {return _cur - _base;}  ///< Maximum byte-offset used
    const size_t available() const      {return _end - _cur;}   ///< Bytes of capacity left

    /// The contents of the heap. You can persist the heap by writing this to a file or socket.
    slice<byte> contents() const        {return {_base, _cur};}

    /// The heap's root value. Starts as Null, but usually an Array or Dict.
    Maybe<Object> root() const;

    /// Sets the heap's root value.
    void setRoot(Maybe<Object>);

    /// Resets the Heap to an empty state.
    void reset();

    void garbageCollectTo(Heap &dstHeap);

    //---- Current Heap:

    /// The current heap of the current thread, or nullptr if none.
    static Heap* current();

    /// Returns the Heap that owns this address, or nullptr if none.
    static Heap* heapContaining(const void *);

    //---- Allocation:

    /// Allocates space for `size` bytes.
    /// If there's not enough space, calls the `AllocFailureHandler` and retries.
    /// If there's no `AllocFailureHandler`, or the handler returns false,
    /// `alloc` returns nullptr.
    ///
    /// Note that if there is a failure handler that runs the garbage collector,
    /// then `alloc` may move objects, invalidating `Object` pointers and `Val`s!
    void* alloc(heapsize size);

    /// Allocates a Block; does not initialize its contents.
    Block* allocBlock(heapsize dataSize, Type);
    /// Allocates a Block and copies the data in `contents` into it, filling the rest with 0.
    Block* allocBlock(heapsize dataSize, Type, slice<byte> contents);

    /// Copies a block, creating a new block with a larger size. The extra bytes are zeroed.
    /// @returns The new block; or the original if the new size is the same as the old;
    ///          or nullptr if the allocation failed.
    Block* growBlock(Block* block, heapsize newDataSize);

    template <ObjectClass OBJ>
    Maybe<OBJ> grow(OBJ const& obj, heapsize newCapacity) {
        Block* b = growBlock(obj.block(), newCapacity * sizeof(typename OBJ::Item));
        if (!b)
            return nullvalue;
        return Object(b).as<OBJ>();
    }

    /// A callback that's invoked when the Heap doesn't have enough space for an allocation.
    /// It should attempt to increase the free space, then return true.
    /// If it can't do anything, it must return false.
    /// The typical things for the callback to do are garbage-collect or grow the heap.
    using AllocFailureHandler = bool(*)(Heap*,heapsize sizeNeeded);

    /// Sets the allocation-failure handler. See @ref AllocFailureHandler for details.
    void setAllocFailureHandler(AllocFailureHandler h)  {_allocFailureHandler = h;}

    /// Changes the size of the heap. All this does is move the end-of-heap pointer;
    /// it doesn't reallocate or move the heap or invalidate any Object pointers.
    /// Returns true on success, false if the new size is illegal.
    /// - It is illegal to grow a malloced heap.
    /// - It is illegal to shrink a Heap smaller than its current `used()` size.
    /// If you grow a non-malloced heap, the new address space at the end must be available
    /// and writeable, otherwise Bad Things will happen when the Heap writes into it.
    bool resize(size_t newSize);

    //---- Address Translation:

    /// Translates a `heappos` offset to a real address.
    void* at(heappos off)               {assert(validPos(off)); return _at(off);}
    const void* at(heappos off) const   {assert(validPos(off)); return _base + uintpos(off);}

    /// Translates a real address to a `heappos` offset.
    heappos pos(const void *ptr) const {assert(ptr >= _base && ptr <= _end); return _pos(ptr);}

    bool contains(const void *ptr) const     {return ptr >= _base && ptr < _cur;}
    bool contains(Object) const;

    /// Returns true if a `heappos` is valid in this Heap, i.e. doesn't point past the end of
    /// allocated memory.
    bool validPos(heappos pos) const;

    SymbolTable const& symbolTable() const {return const_cast<Heap*>(this)->symbolTable();}
    SymbolTable& symbolTable();

    using BlockVisitor = function_ref<bool(const Block&)>;
    using ObjectVisitor = function_ref<bool(const Object&)>;

    /// Calls the Visitor callback once for each live (reachable) block.
    void visitBlocks(BlockVisitor);
    void visit(ObjectVisitor);

    /// Calls the Visitor callback once for each object, even if it's unreachable (garbage).
    void visitAll(BlockVisitor const&);

    /// Calls the Visitor callback once for each known garbage-collection root.
    /// This includes the heap's root, its SymbolTable's array, and any registered external roots.
    void visitRoots(BlockVisitor const&);

    void dump(std::ostream&);

    void registerExternalRoot(Value*) const;
    void unregisterExternalRoot(Value*) const;
    void registerExternalRoot(Object*) const;
    void unregisterExternalRoot(Object*) const;

    void registerExternalRoots(Val rootArray[], size_t count);
    void unregisterExternalRoots(Val rootArray[]);

private:
    friend class Block;
    friend class SymbolTable;
    friend class GarbageCollector;
    friend class UsingHeap;
    friend class HandleBase;
    struct Header;

    Heap();
    explicit Heap(const char *error);
    Heap(void *base, size_t capacity, bool malloced);
    Header& header() pure                {assert(_base); return *(Header*)_base;}
    Header const& header() const pure    {assert(_base); return *(Header*)_base;}
    void* _at(heappos off) pure         {return _base + uintpos(off);}
    heappos _pos(const void *ptr) const pure {return heappos((byte*)ptr - _base);}
    Value posToValue(heappos pos) const pure;
    heappos valueToPos(Value obj) const pure;
    void registr();
    void unregistr();
    void clearForwarding();
    Heap const* enter() const;
    void exit() const;
    void exit(Heap const* newCurrent) const;
    const char* _validate() const;

    // Allocates space without initializing it. Caller MUST initialize (see Block constructor)
    void* rawAlloc(heapsize size);

    void* rawAllocFailed(heapsize size);

    Block const* firstBlock() const;
    Block const* nextBlock(Block const*) const;

    Value symbolTableArray() const;
    void setSymbolTableArray(Value);

    void swapMemoryWith(Heap&);

    byte*   _base;
    byte*   _end;
    byte*   _cur;
    AllocFailureHandler _allocFailureHandler = nullptr;
    std::vector<Value*> mutable _externalRootVals;
    std::vector<Object*> mutable _externalRootObjs;
    std::unique_ptr<SymbolTable> _symbolTable;
    mutable const char* _error = nullptr;
    bool    _malloced = false;
};



/// Makes a heap current (on this thread) while in scope.
/// When it exits scope, the previously-current heap is restored.
class UsingHeap {
public:
    explicit UsingHeap(Heap const* heap)    :_heap(heap) {_prev = heap->enter();}
    explicit UsingHeap(Heap const& heap)    :UsingHeap(&heap) { }
    ~UsingHeap()                            {_heap->exit(_prev);}
private:
    Heap const* _heap;
    Heap const* _prev;
};



/// A Handle is an object reference that is known to the Heap; during a garbage collection it
/// will be updated to point to the new location of the object.
/// `OBJ` may be `Object` or any subclass, or `Value`.
template <class OBJ>
class Handle : public OBJ {
public:
    Handle()                            :Handle(*Heap::current()) { }
    explicit Handle(Heap &heap)         :OBJ(),  _heap(&heap) {reg();}
    Handle(OBJ const& o)                :Handle(o, *Heap::current()) { }
    Handle(OBJ const& o, Heap &heap)    :OBJ(o), _heap(&heap) {reg();}
    ~Handle()                           {unreg();}

    Handle(Handle const& h)             :Handle(h, *h._heap) { }
    Handle& operator=(Handle const& h)  {unreg(); OBJ::operator=(h); _heap = h._heap; reg(); return *this;}

    Handle& operator=(Object o)         {Object::operator=(o); return *this;}

    void setHeap(Heap &heap)            {_heap = &heap;}
private:
    void reg()                          {_heap->registerExternalRoot(this);}
    void unreg()                        {_heap->unregisterExternalRoot(this);}
    Heap* _heap;
};


/// Specialization of Handle for `Maybe<>` types.
template <class OBJ>
class Handle<Maybe<OBJ>> : public Maybe<OBJ> {
public:
    Handle()                            :Handle(*Heap::current()) { }
    explicit Handle(Heap &heap)         :Maybe<OBJ>(),  _heap(&heap) {reg();}
    Handle(Maybe<OBJ> const& m)         :Handle(m, *Heap::current()) { }
    Handle(Maybe<OBJ> const& m, Heap &h):Maybe<OBJ>(m), _heap(&h) {reg();}
    ~Handle()                           {unreg();}

    Handle(Handle const& h)             :Handle(h, *h._heap) { }
    Handle& operator=(Handle const& h)  {unreg(); OBJ::operator=(h); _heap = h._heap; reg(); return *this;}

    Handle& operator=(OBJ o)            {Maybe<OBJ>::operator=(o); return *this;}
    Handle& operator=(Maybe<OBJ> const&m) {Maybe<OBJ>::operator=(m); return *this;}

    void setHeap(Heap &heap)            {_heap = &heap;}
private:
    void reg()                          {_heap->registerExternalRoot(&this->_obj);}
    void unreg()                        {_heap->unregisterExternalRoot(&this->_obj);}
    Heap* _heap;
};


/// A variant form of Handle that registers a _separate_ Object or Value variable as a root;
/// keep using that variable. Example:
///     String str = newString("foo", heap);
///     Handle h(&str);
///     GarbageCollector::run(heap);  // the Handle will update str
///     auto s = str.str();           // doesn't crash!
template <class OBJ>
class Handle<OBJ*> {
public:
    explicit Handle(OBJ* o)             :Handle(o, *Heap::current()) { }
    Handle(OBJ* o, Heap &heap)          :_objp(o), _heap(&heap) {reg();}
    ~Handle()                           {unreg();}
private:
    Handle(const Handle&) = delete;
    Handle& operator=(Handle const&) = delete;
    void reg()   {_heap->registerExternalRoot(_objp);}
    void unreg() {_heap->unregisterExternalRoot(_objp);}

    OBJ*  _objp;
    Heap* _heap;
};

}
