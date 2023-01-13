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

    heapsize capacity() const   {return _count & CountMask;}
    heapsize count() const      {return capacity();}        // Dict "overrides" this
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

    static void* operator new(size_t size, IN_MUT_HEAP, size_t extra) {
        assert(size + extra < Heap::MaxSize);
        if (!heap)
            heap = Heap::current();
        return heap->alloc(heapsize(size + extra));
    }
    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    explicit Object(heapsize capacity)              :_count(capacity) { }

    Object(size_t count, const void *items, size_t itemSize)
    :Object(heapsize(count))
    {
        assert(count <= MaxCount);
        size_t size = count * itemSize;
        if (items)
            ::memcpy(_items, items, size);
        else
            ::memset(_items, 0, size);
    }

    void* items()                {return _items;}
    const void* items() const    {return _items;}

    // Used by GC:
    heappos getForwardingAddress() const {
        return hasFlag(GC) ? (_count & ~GC) : 0;
    }

    void setForwardingAddress(heappos fwd) {
        assert(fwd > 0);
        assert(!hasFlag(GC));
        _count = GC | fwd;
    }

private:
    heapsize _count;
    byte     _items[1]; // array size is a placeholder; really variable size
};




// Abstract base of String, Array, Dict
template <Val::TagBits TAG>
class TaggedObject : public Object {
public:
    static constexpr Val::TagBits kTag = TAG;
protected:
    explicit TaggedObject(heapsize capacity) :Object(capacity) { }
    TaggedObject(size_t count, const void *items, size_t itemSize) :Object(count, items, itemSize) { }
};


// Abstract base of String, Array, Dict
template <class T, typename ITEM, Val::TagBits TAG>
class Collection : public TaggedObject<TAG> {
public:
    using Item = ITEM;

    Val asVal(IN_HEAP) const        {return Val(this, heap);}

    using iterator = Item*;
    using const_iterator = const Item*;

    const_iterator begin() const    {return data();}
    const_iterator end() const      {return data() + Object::count();}

protected:
    explicit Collection(heapsize capacity) :TaggedObject<TAG>(capacity) { }
    Collection(size_t count, const void *items, size_t itemSize) :TaggedObject<TAG>(count, items, itemSize) { }

    static T* createUninitialized(heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Item)) T(capacity);
    }

    Item* data()                    {return (Item*)Object::items();}
    const Item* data() const        {return (const Item*)Object::items();}

    iterator begin()                {return data();}
    iterator end()                  {return data() + Object::count();}
};


/// A string object. Stores UTF-8 characters, no nul termination.
class String : public Collection<String, char, Val::StringTag> {
public:
    static String* create(const char *str, size_t size, IN_MUT_HEAP) {
        return new (heap, size) String(str, size);
    }

    static String* create(string_view str, IN_MUT_HEAP) {
        return create(str.data(), str.size(), heap);
    }

    const char* data() const        {return Collection::data();}
    string_view get() const         {return string_view(data(), count());}

private:
    template <class T, typename ITEM, Val::TagBits TAG> friend class Collection;
    friend class GarbageCollector;

    explicit String(heapsize capacity)   :Collection(capacity) { }
    String(const char *str, size_t size) :Collection(size, str, 1) { }

    template <typename LAMBDA>      // (used only by the GC)
    void populate(const char *chars, const char *endChars, LAMBDA remap) {
        memcpy((char*)data(), chars, endChars - chars);
    }
};


/// An array of `Val`s.
class Array : public Collection<Array, Val, Val::ArrayTag> {
public:
    static Array* create(heapsize count, IN_MUT_HEAP) {
        return new (heap, count * sizeof(Val)) Array(nullptr, count);
    }
    static Array* create(std::initializer_list<Val> vals, IN_MUT_HEAP) {
        return new (heap, vals.size() * sizeof(Val)) Array(vals.begin(), vals.size());
    }

    /// Direct access to array of Val
    Val* data()              {return Collection::data();}
    const Val* data() const  {return Collection::data();}

    iterator begin()                {return Collection::begin();}
    iterator end()                  {return Collection::end();}
    const_iterator begin() const    {return Collection::begin();}
    const_iterator end() const      {return Collection::end();}

    Val& operator[] (heapsize i)        {assert(i < count()); return data()[i];}
    Val  operator[] (heapsize i) const  {assert(i < count()); return data()[i];}

private:
    template <class T, typename ITEM, Val::TagBits TAG> friend class Collection;
    friend class GarbageCollector;

    explicit Array(heapsize capacity)    :Collection(capacity) { }
    Array(const Val *vals, size_t count) :Collection(count, vals, sizeof(Val)) { }

    template <typename LAMBDA>      // (used only by the GC)
    void populate(const Val *ents, const Val *endEnts, LAMBDA remap) {
        auto dst = begin();
        for (auto src = ents; src < endEnts; ++src)
            *dst++ = remap(*src);
    }
};


struct DictEntry {
    Val const key;
    Val       value;
};

/// A dictionary mapping strings to `Val`s.
class Dict : public Collection<Dict, DictEntry, Val::DictTag> {
public:
    static Dict* create(heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Val)) Dict(capacity, nullptr, 0);
    }
    static Dict* create(std::initializer_list<DictEntry> vals, IN_MUT_HEAP) {
        return create(vals, heapsize(vals.size()), heap);
    }
    static Dict* create(std::initializer_list<DictEntry> vals, heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Val)) Dict(capacity, vals.begin(), vals.size());
    }

    heapsize capacity() const   {return Object::count();}
    bool full() const           {return (end() - 1)->key != nullval;}
    bool empty() const          {return begin()->key == nullval;}
    heapsize count() const      {return heapsize(end() - begin());}
    size_t size() const         {return count();}

    Val* find(Val keyStr);
    const Val* find(Val keyStr) const           {return const_cast<Dict*>(this)->find(keyStr);}
    Val get(Val keyStr) const                   {auto v = find(keyStr); return v ? *v : nullval;}
    bool contains(Val keyStr) const             {return find(keyStr) != nullptr;}

    bool set(Val keyStr, Val value);
    bool replace(Val keyStr, Val newValue);
    bool remove(Val keyStr);

    Val operator[] (Val keyStr) const           {return get(keyStr);}

    iterator begin()                {return data();}
    iterator end()                  {return _findEntry(nullval);}
    const_iterator begin() const    {return data();}
    const_iterator end() const      {return const_cast<Dict*>(this)->end();}

    static bool keyCmp(DictEntry const& a, DictEntry const& b) {return Val::keyCmp(a.key, b.key);}

private:
    template <class T, typename ITEM, Val::TagBits TAG> friend class Collection;
    friend class GarbageCollector;

    explicit Dict(heapsize capacity)   :Collection(capacity) { }

    Dict(heapsize capacity, const DictEntry *ents, size_t count)
    :Collection(capacity, nullptr, sizeof(DictEntry))
    {
        assert(capacity > 0 && capacity >= count);
        if (count > 0) {
            ::memcpy(begin(), ents, count * sizeof(DictEntry));
            sort(count);
        }
    }

    template <typename LAMBDA>      // (used only by the GC)
    void populate(const DictEntry *ents, const DictEntry *endEnts, LAMBDA remap) {
        assert(empty());
        assert(endEnts - ents == capacity());
        auto dst = begin();
        for (auto src = ents; src < endEnts; ++src)
            new (dst++) DictEntry{remap(src->key), remap(src->value)};
        sort(endEnts - ents);
    }

    DictEntry* _findEntry(Val keyStr);
    void sort(size_t count);
    iterator endAll()                               {return begin() + capacity();}
};


template <class T> T* Val::as(Heap const* heap) const {
    assert(tag() == T::kTag);
    return (T*)heap->at(asPos());
}




std::ostream& operator<<(std::ostream&, String const*);
std::ostream& operator<<(std::ostream&, Array const*);
static inline std::ostream& operator<<(std::ostream& out, String const& str) {return out << &str;}
static inline std::ostream& operator<<(std::ostream& out, Array const& arr) {return out << &arr;}



template <class T> void GarbageCollector::update(T*& obj) {
    Val val = obj->asVal(&_fromHeap);
    update(val);
    obj = val.as<T>(&_toHeap);
}
