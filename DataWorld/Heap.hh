//
// Heap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>


class Object;
class Val;


using byte = std::byte;

using  intpos =  int32_t;
using uintpos = uint32_t;

using heapsize = uintpos;      ///< Like `size_t` for Heaps.

enum class heappos : uintpos { };      ///< A position in a Heap, relative to its base.

static inline heappos operator+ (heappos p, intpos i) {return heappos(uintpos(p) + i);}
static inline heappos operator- (heappos p, intpos i) {return heappos(uintpos(p) - i);}
static inline std::strong_ordering operator<=> (heappos p, size_t i) {return uintpos(p) <=> i;}
static inline std::strong_ordering operator<=> (heappos p, intpos i) {return int64_t(p) <=> int64_t(i);}


/// A simple container for dynamic allocation.
/// Pointers within a Heap are 32-bit values, offsets from the heap's base address.
/// Allocation uses a simple bump (arena) allocator. Allocations are 4-byte aligned.
class Heap {
public:
    static constexpr int AlignmentBits  = 2;
    static constexpr int Alignment  = 1 << AlignmentBits;
    static constexpr uintptr_t AlignmentMask  = (1 << AlignmentBits) - 1;

    static constexpr size_t  MaxSize = 1 << 31;

    // Constructs a new empty Heap starting at address `base` and capacity `size`.
    Heap(void *base, size_t capacity)   :Heap(base, capacity, false) {reset();}

    // Constructs a new empty Heap with space allocated by malloc.
    explicit Heap(size_t capacity)      :Heap(::malloc(capacity), capacity, true) {reset();}

    // Constructs a Heap from already-existing heap data. Throws if the data is not valid.
    static Heap existing(void *base, size_t used, size_t capacity);

    ~Heap()                             {if (_malloced) free(_base);}

    const void*  base() const           {return _base;}         ///< Address of start of heap.
    const size_t capacity() const       {return _end - _base;}  ///< Maximum size it can grow to
    const size_t used() const           {return _cur - _base;}  ///< Maximum byte-offset used
    const size_t remaining() const      {return _end - _cur;}   ///< Bytes of capacity left

    /// The heap's root value. Starts as Null, but usually an Array or Dict.
    Val root() const;

    /// Sets the heap's root value.
    void setRoot(Val);

    template <class T> void setRoot(T* obj)     {setRoot(obj->asVal(this));}
    template <class T> T* getRootAs() const;

    /// Resets the Heap to an empty state.
    void reset();

    void garbageCollectTo(Heap &dstHeap);

    //---- Current Heap:

    /// Makes this the current heap of the current thread.
    void enter();

    /// Clears the current heap.
    void exit();

    /// The current heap of the current thread, or nullptr if none.
    static Heap* current();

    //---- Allocation:

    /// Allocates space for `size` bytes. The address will be 4-byte aligned.
    /// If there's not enough space, calls the AllocFailureHandler and retries.
    /// If there's no AllocFailureHandler, or it returns false, throws an exception.
    void* alloc(heapsize size) {
        byte *result, *newCur;
        do {
            result = alignUp(_cur);
            newCur = result + size;
            if (newCur > _end) {
                if (!_allocFailureHandler || !_allocFailureHandler(this, size))
                    throw std::runtime_error("Heap overflow");
            }
        } while (newCur > _end);
        _cur = newCur;
        return result;
    }

    /// Convenience wrapper to allocate an instance of `T`.
    template <typename T>
    T* alloc()                      {return (T*)alloc(sizeof(T));}

    /// A callback that's invoked when the Heap doesn't have enough space for an allocation.
    /// It should attempt to increase the free space, then return true.
    /// If it can't, it must return false.
    using AllocFailureHandler = bool(*)(Heap*,heapsize sizeNeeded);

    /// Sets the allocation-failure handler.
    void setAllocFailureHandler(AllocFailureHandler h)  {_allocFailureHandler = h;}

    //---- Address Translation:

    /// Translates a `heappos` offset to a real address.
    void* at(heappos off)               {assert(validPos(off)); return _base + uintpos(off);}
    const void* at(heappos off) const   {assert(validPos(off)); return _base + uintpos(off);}

    /// Translates a real address to a `heappos` offset.
    heappos pos(const void *ptr) const {
        assert(ptr >= _base && ptr < _end && isAligned(ptr));
        return heappos((byte*)ptr - _base);
    }

    /// Returns true if a `heappos` is valid in this Heap, i.e. doesn't point past the end of
    /// allocated memory.
    bool validPos(heappos pos) const;

    static inline bool isAligned(const void *ptr)   {return (uintptr_t(ptr) & AlignmentMask) == 0;}
    static inline bool isAligned(heappos pos)       {return (uintpos(pos) & AlignmentMask) == 0;}

    template <typename T> static T* alignUp(T *addr) {
        return (T*)((uintptr_t(addr) + AlignmentMask) & ~AlignmentMask);
    }

    Object* firstObject();
    Object* nextObject(Object *obj);

    using Visitor = std::function<bool(Val)>;

    void visit(Visitor const&);

private:
    friend class GarbageCollector;
    
    Heap(void *base, size_t capacity, bool malloced);
    void clearObjectFlags(heapsize /*Object::Flags*/ flags);

    byte*   _base;
    byte*   _end;
    byte*   _cur;
    AllocFailureHandler _allocFailureHandler = nullptr;
    bool    _malloced = false;
};


class ConstHeapRef {
public:
    ConstHeapRef()                  :_heap(Heap::current()) { }
    ConstHeapRef(nullptr_t)         :ConstHeapRef() { }
    ConstHeapRef(Heap const* h)     :_heap(h) { }
    ConstHeapRef(Heap const& h)     :_heap(&h) { }

    Heap const* operator* ()    {return _heap;}
    Heap const* operator->()    {return _heap;}

protected:
    Heap const* _heap;
};


class HeapRef : ConstHeapRef {
public:
    HeapRef()           :ConstHeapRef() { }
    HeapRef(nullptr_t)  :ConstHeapRef() { }
    HeapRef(Heap *h)    :ConstHeapRef(h) { }
    HeapRef(Heap &h)    :ConstHeapRef(h) { }

    Heap* operator* () const    {return (Heap*)_heap;}
    Heap* operator->() const    {return (Heap*)_heap;}
};


#define IN_MUT_HEAP HeapRef heap //= nullptr
#define IN_HEAP     ConstHeapRef heap //= nullptr

//static inline Heap* GetHeap(Heap* h)              {return h ? h : Heap::current();}
//static inline Heap const* GetHeap(Heap const* h)  {return h ? h : Heap::current();}



/// A typical copying garbage collector that copies all live objects into another Heap.
/// At the end it swaps the memory of the two Heaps, so the original heap is now clean,
/// and the other heap can be freed or reused for the next GC.
class GarbageCollector {
public:
    /// Constructs the GC and copies all Values reachable from the root into a temporary Heap
    /// with the same capacity as this one.
    GarbageCollector(Heap &heap);

    /// Constructs the GC and copies all Values reachable from the root into `otherHeap`.
    GarbageCollector(Heap &heap, Heap &otherHeap);

    /// Updates an existing Val that came from the "from" heap,
    /// returning an equivalent Val that's been copied to the "to" heap.
    /// You MUST call this, or any of the `update` methods below,
    /// on any live references to values in `fromHeap`, or they'll be out of date.
    ///
    /// Do not do anything else with the heap while the GarbageCollector is in scope!
    Val scanValue(Val v);

    // These are equivalent to scanValue but update the Val/Ptr/Object in place:
    void update(Val&);
//    template <class T> void update(Ptr<T>& ptr);
    template <class T> void update(T*&);

    // The destructor swaps the two heaps, so _fromHeap is now the live one.
    ~GarbageCollector()     {_fromHeap.reset(); std::swap(_fromHeap, _toHeap);}

private:
    void scanRoot();
    template <class T> Val scanValueAs(Val val);

    std::unique_ptr<Heap> _tempHeap;
    Heap &_fromHeap, &_toHeap;
};
