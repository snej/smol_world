//
// Objects.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Val.hh"
#include <initializer_list>
#include <string_view>


using string_view = std::string_view;


/// Abstract base of classes that live in a Heap and are referred to with Vals.
class Object {
public:
    static constexpr heapsize MaxCount = (1<<28) - 1;

    heapsize count() const      {return _count & CountMask;}
    size_t size() const         {return count();}
    bool empty() const          {return count() == 0;}

    enum Flags : heapsize {
        GC = 0x80000000,        // Must be 0 except during garbage collection
    };

    Flags flags() const                             {return Flags(_count);}
    bool hasFlag(Flags f) const                     {return (_count & f) != 0;}
    void setFlag(Flags f, bool state)               {if (state) _count |= f; else _count &= ~f;}

protected:
    friend class Heap;
    friend class GarbageCollector;
    
    static constexpr heapsize CountMask = 0x0FFFFFFF;

    static void* operator new(size_t size, Heap *heap, size_t extra) {
        assert(size + extra < Heap::MaxSize);
        if (!heap)
            heap = Heap::current();
        return heap->alloc(heapsize(size + extra));
    }
    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    Object(size_t count, const void *items, size_t itemSize)
    :_count(uint32_t(count)) {
        assert(count <= MaxCount);
        size_t size = count * itemSize;
        if (items)
            ::memcpy(_items, items, size);
        else
            ::memset(_items, 0, size);
    }

    template <typename T> T* items()                {return (T*)_items;}
    template <typename T> T const* items() const    {return (T const*)_items;}

    // Used by GC:
    PtrBase getForwardingAddress() const {
        return PtrBase(hasFlag(GC) ? (_count & ~GC) : 0);
    }

    void setForwardingAddress(PtrBase fwd) {
        assert(!hasFlag(GC));
        _count = GC | fwd.pos();
    }

private:
    heapsize _count;
    byte     _items[1]; // array size is a placeholder; really variable size
};


/// A string object. Stores UTF-8 characters, no nul termination.
class String : public Object {
public:
    static String* create(const char *str, size_t size, Heap *heap /*= nullptr*/) {
        return new (heap, size) String(str, size);
    }

    static String* create(string_view str, Heap *heap /*= nullptr*/) {
        return create(str.data(), str.size(), heap);
    }

    string_view get() const {return string_view(items<char>(), count());}

    Ptr<String> ptr(Heap *heap) {return Ptr(this, heap);}

    static constexpr Val::TagBits kTag = Val::StringTag;

private:
    String(const char *str, size_t size) :Object(size, str, 1) { }
};


/// An array of `Val`s.
class Array : public Object {
public:
    static Array* create(heapsize count, Heap *heap /*= nullptr*/) {
        return new (heap, count * sizeof(Val)) Array(nullptr, count);
    }
    static Array* create(std::initializer_list<Val> vals, Heap *heap /*= nullptr*/) {
        return new (heap, vals.size() * sizeof(Val)) Array(vals.begin(), vals.size());
    }

    Val* get()              {return items<Val>();}
    const Val* get() const  {return items<Val>();}

    Ptr<Array> ptr(Heap *heap) {return Ptr(this, heap);}

    Val& operator[] (heapsize i)  {assert(i < count()); return get()[i];}

    using iterator = Val*;
    using const_iterator = const Val*;

    iterator begin()                {return get();}
    iterator end()                  {return get() + count();}
    const_iterator begin() const    {return get();}
    const_iterator end() const      {return get() + count();}

    static constexpr Val::TagBits kTag = Val::ArrayTag;

private:
    Array(const Val *vals, size_t count) :Object(count, vals, sizeof(Val)) { }
};


/// A dictionary mapping strings to `Val`s.
class Dict : public Object {
public:
    static constexpr Val::TagBits kTag = Val::DictTag;
};


template <class T> Ptr<T> Val::asPtr() const {
    assert(tag() == T::kTag);
    return Ptr<T>(asPos());
}

template <class T> T* Val::as(Heap const* heap) {
    return asPtr<T>().get(heap);
}




std::ostream& operator<<(std::ostream&, String const*);
std::ostream& operator<<(std::ostream&, Array const*);
static inline std::ostream& operator<<(std::ostream& out, String const& str) {return out << &str;}
static inline std::ostream& operator<<(std::ostream& out, Array const& arr) {return out << &arr;}
