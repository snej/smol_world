//
// Ptr.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Heap.hh"


/// An untyped 'pointer' within a Heap.
/// It's represented as a 32-bit offset from the start of the heap.
class PtrBase {
public:
    PtrBase(void const* addr, IN_HEAP)      :_pos(heap->pos(addr)) { }
    explicit PtrBase(heappos pos)           :_pos(pos) {assert(Heap::isAligned(pos)); }
    heappos pos() const                     {return _pos;}
    void* get(IN_HEAP) const                {return heap->at(_pos);}
    const void* get(IN_HEAP) const          {return heap->at(_pos);}

    explicit operator bool() const          {return _pos != 0;}
private:
    heappos _pos;
};


/// A 'pointer' within a heap, to a value of type `T` (in practice, an `Object` subclass.)
template <class T>
class Ptr : public PtrBase {
public:
    Ptr(T const* addr, IN_HEAP)         :PtrBase(addr, heap) { }
    explicit Ptr(heappos pos)           :PtrBase(pos) { }
    explicit Ptr(PtrBase ptr)           :PtrBase(ptr) { }

    T* get(IN_HEAP) const               {return (T*)PtrBase::get(heap);}

    //    T* operator* () const               {return get();}
    //    T* operator-> () const              {return get();}
};

