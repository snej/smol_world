//
// Heap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>


using byte = std::byte;

using heappos  = uint32_t;      ///< A position in a Heap, relative to its base.
using heapsize = uint32_t;      ///< Like `size_t` for Heaps.

class Object;
class PtrBase;
template <class T> class Ptr;
class Val;


/// A simple container for dynamic allocation.
/// Intra-Heap pointers use `Ptr`, a 32-bit tagged pointer.
/// Allocation uses a simple bump / arena allocator. Allocations are 4-byte aligned.
class Heap {
public:
    static constexpr heappos AlignmentBits  = 2;
    static constexpr heappos Alignment  = 1 << AlignmentBits;
    static constexpr uintptr_t AlignmentMask  = (1 << AlignmentBits) - 1;

    static constexpr size_t  MaxSize = 1 << 31;

    // Constructs a new empty Heap starting at address `base` and capacity `size`.
    Heap(void *base, size_t capacity)   :Heap(base, capacity, false) {reset();}

    // Constructs a new empty Heap with space allocated by malloc.
    explicit Heap(size_t capacity)      :Heap(::malloc(capacity), capacity, true) {reset();}

    // Constructs a Heap from already-existing heap data. Throws if the data is not valid.
    static Heap existing(void *base, size_t used, size_t capacity);

    ~Heap()                             {if (_malloced) free(_base);}

    const void* base() const            {return _base;}
    const size_t capacity() const       {return _end - _base;}
    const size_t used() const           {return _cur - _base;}
    const size_t remaining() const      {return _end - _cur;}

    /// The heap's root value. Starts as Null, but usually an Array or Dict.
    Val root() const;

    /// Sets the heap's root value.
    void setRoot(Val);

    template <class T> void setRoot(T* obj)     {setRoot(obj->asVal(this));}
    template <class T> T* getRootAs() const;

    /// Resets the Heap to an empty state.
    void reset();

    void garbageCollectTo(Heap &dstHeap);

    /// Swaps the memory of two heaps. Useful after GC.
    friend void swap(Heap &a, Heap &b) {
        std::swap(a._base, b._base);
        std::swap(a._end, b._end);
        std::swap(a._cur, b._cur);
        std::swap(a._malloced, b._malloced);
    }

    //---- Current Heap:

    void enter();
    void exit();

    static Heap* current();

    //---- Allocation:

    void* alloc(heapsize size) {
        byte *result = (byte*)((uintptr_t(_cur) + AlignmentMask) & ~AlignmentMask);
        byte* newCur = result + size;
        if (newCur > _end) throw std::runtime_error("Heap overflow");
        _cur = newCur;
        return result;
    }

    template <typename T>
    T* alloc()                      {return (T*)alloc(sizeof(T));}

    //---- Address Translation:

    void* at(heappos off)               {assert(validPos(off)); return _base + off;}
    const void* at(heappos off) const   {assert(validPos(off)); return _base + off;}

    heappos pos(const void *ptr) const {
        assert(ptr >= _base && ptr < _end && isAligned(ptr));
        return heappos((byte*)ptr - _base);
    }

    bool validPos(heappos pos) const;

    static inline bool isAligned(const void *ptr)   {return (uintptr_t(ptr) & AlignmentMask) == 0;}
    static inline bool isAligned(heappos pos)       {return (pos & AlignmentMask) == 0;}

private:

    Heap(void *base, size_t capacity, bool malloced);
    Val _gcCopy(Val, Heap&);

    byte*   _base;
    byte*   _end;
    byte*   _cur;
    bool    _malloced = false;
};


#define IN_MUT_HEAP Heap* heap //= nullptr
#define IN_HEAP     Heap const* heap //= nullptr

static inline Heap* GetHeap(Heap* h)              {return h ? h : Heap::current();}
static inline Heap const* GetHeap(Heap const* h)  {return h ? h : Heap::current();}




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
    template <class T> void update(Ptr<T>& ptr);
    template <class T> void update(T*&);

    // The destructor swaps the two heaps, so _fromHeap is now the live one.
    ~GarbageCollector()     {_fromHeap.reset(); swap(_fromHeap, _toHeap);}

private:
    void scanRoot();
    template <class T> Val scanValueAs(Val val);

    
    std::unique_ptr<Heap> _tempHeap;
    Heap &_fromHeap, &_toHeap;
};
