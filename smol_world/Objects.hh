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

    enum Flags : heapsize {
        Fwd     = 0x80000000,       // Means the object has moved to a different address; used by GC
        Visited = 0x40000000,       // Visited during Heap::visit()
        _spare1 = 0x20000000,
        _spare2 = 0x10000000,
    };

    Flags flags() const                             {return Flags(_meta);}
    bool hasFlag(Flags f) const                     {return (_meta & f) != 0;}

protected:
    friend class Heap;
    friend class GarbageCollector;

    void setFlag(Flags f, bool state)               {if (state) _meta |= f; else _meta &= ~f;}

    static constexpr heapsize MetaMask = 0x0FFFFFFF;

    static void* operator new(size_t size, IN_MUT_HEAP, size_t extra) {
        assert(size + extra < Heap::MaxSize);
        return heap->alloc(heapsize(size + extra));
    }

    explicit Object(heapsize totalSize)         :_meta(totalSize - sizeof(Object)) { }

    /// The integer stored in the object header (minus the flags.)
    /// Collections use this as an item count; other classes can use it for other purposes.
    heapsize dataSize() const                   {return _meta & MetaMask;}

    /// A pointer to just after the object header (the `_meta` field.) Used by collections.
    void* data()                                {return &_meta + 1;}
    const void* data() const                    {return &_meta + 1;}

    // Used by GC:
    heappos getForwardingAddress() const        {return heappos(hasFlag(Fwd) ? (_meta & ~Fwd) : 0);}

    void setForwardingAddress(heappos fwd) {
        assert(fwd > 0 && !(uintpos(fwd) & Fwd));
        assert(!hasFlag(Fwd));
        _meta = Fwd | uintpos(fwd);
    }

private:
    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    heapsize _meta;
};



// An Object that can be tagged by a Val. Defines the `Tag` class property used by various macros.
template <Val::TagBits TAG>
class TaggedObject : public Object {
public:
    static constexpr Val::TagBits Tag = TAG;

    Val asVal(IN_HEAP) const    {return Val(this, heap);}

protected:
    explicit TaggedObject(heapsize totalSize) :Object(totalSize) { }
};



// Abstract base of String, Array, Dict
template <class T, typename ITEM, Val::TagBits TAG>
class Collection : public TaggedObject<TAG> {
public:
    using Item = ITEM;

    static constexpr heapsize MaxCount = ((1<<28) / sizeof(ITEM)) - 1;

    heapsize capacity() const   {return Object::dataSize() / sizeof(Item);}
    heapsize count() const      {return capacity();}        // Dict "overrides" this
    size_t size() const         {return count();}
    bool empty() const          {return count() == 0;}

    using iterator = Item*;
    using const_iterator = const Item*;

    const_iterator begin() const    {return data();}
    const_iterator end() const      {return data() + capacity();}

protected:
    explicit Collection(heapsize capacity)
    :TaggedObject<TAG>(sizeof(Collection) + capacity * sizeof(Item)) { }

    Collection(size_t count, const Item *items)
    :Collection(heapsize(count))
    {
        assert(count <= MaxCount);
        size_t size = count * sizeof(Item);
        if (items)
            ::memcpy(data(), items, size);
        else
            ::memset(data(), 0, size);
    }

    static T* createUninitialized(heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Item)) T(capacity);
    }

    Item* data()                    {return (Item*)Object::data();}
    const Item* data() const        {return (const Item*)Object::data();}

    iterator begin()                {return data();}
    iterator end()                  {return data() + capacity();}
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
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
    String(const char *str, size_t size) :Collection(size, str) { }

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
    Val* data()                     {return Collection::data();}
    const Val* data() const         {return Collection::data();}

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
    Array(const Val *vals, size_t count) :Collection(count, vals) { }

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


/// A key-value mapping.
/// Keys can be anything but null, and are compared by identity (i.e. two Strings with the same
/// contents are not the same key!) Values can be anything including null.
class Dict : public Collection<Dict, DictEntry, Val::DictTag> {
public:
    /// Creates an empty dictionary with the given capacity.
    static Dict* create(heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Val)) Dict(capacity, nullptr, 0);
    }
    /// Creates a dictionary from a list of key-value pairs. It will have no extra capacity.
    static Dict* create(std::initializer_list<DictEntry> vals, IN_MUT_HEAP) {
        return create(vals, heapsize(vals.size()), heap);
    }
    /// Creates a dictionary from a list of key-value pairs.
    /// The capacity must be at least the number of pairs but can be larger.
    static Dict* create(std::initializer_list<DictEntry> vals, heapsize capacity, IN_MUT_HEAP) {
        return new (heap, capacity * sizeof(Val)) Dict(capacity, vals.begin(), vals.size());
    }

    heapsize capacity() const           {return Collection::capacity();}
    bool full() const                   {return (end() - 1)->key != nullval;}
    bool empty() const                  {return begin()->key == nullval;}
    heapsize count() const              {return heapsize(end() - begin());}
    size_t size() const                 {return count();}

    Val* find(Val key);
    const Val* find(Val key) const      {return const_cast<Dict*>(this)->find(key);}
    Val get(Val key) const              {auto v = find(key); return v ? *v : nullval;}
    bool contains(Val key) const        {return find(key) != nullptr;}

    bool set(Val key, Val value);
    bool replace(Val key, Val newValue);
    bool remove(Val key);

    Val operator[] (Val key) const      {return get(key);}

    iterator begin()                    {return data();}
    iterator end()                      {return _findEntry(nullval);}
    const_iterator begin() const        {return data();}
    const_iterator end() const          {return const_cast<Dict*>(this)->end();}

    static bool keyCmp(DictEntry const& a, DictEntry const& b) {return Val::keyCmp(a.key, b.key);}

private:
    template <class T, typename ITEM, Val::TagBits TAG> friend class Collection;
    friend class GarbageCollector;

    explicit Dict(heapsize capacity)   :Collection(capacity) { }

    Dict(heapsize capacity, const DictEntry *ents, size_t count)
    :Collection(capacity, nullptr)
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

    DictEntry* _findEntry(Val key);
    void sort(size_t count);
    iterator endAll()                               {return begin() + capacity();}
};


template <class T> T* Val::as(ConstHeapRef heap) const {
    assert(tag() == T::Tag);
    return (T*)asObject(heap);
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
