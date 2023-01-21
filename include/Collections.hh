//
// Collections.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Object.hh"


/// Abstract base of String, Array, Dict
template <class T, typename ITEM, Type TYPE>
class Collection : public TypedObject<TYPE> {
public:
    using Item = ITEM;

    static constexpr heapsize MaxCount = (Object::MaxSize / sizeof(Item)) - 1;

    heapsize capacity() const   {return Object::dataSize() / sizeof(Item);}
    heapsize count() const      {return capacity();}        // Dict "overrides" this
    size_t size() const         {return count();}
    bool empty() const          {return count() == 0;}

    using iterator = Item*;
    using const_iterator = const Item*;

    const_iterator begin() const    {return items().begin();}
    const_iterator end() const      {return items().end();}
    iterator begin()                {return items().begin();}
    iterator end()                  {return items().end();}

    slice<Item> items()             {return this->template data<Item>();}
    slice<Item> items() const       {return const_cast<Collection*>(this)->items();}

protected:
    static T* createUninitialized(size_t capacity, IN_MUT_HEAP) {
        void* addr = Object::alloc(sizeof(T), heap, capacity * sizeof(Item));
        return addr ? new (addr) T(heapsize(capacity)) : nullptr;
    }
    static T* create(size_t capacity, const Item* data, size_t count, IN_MUT_HEAP) {
        void* addr = Object::alloc(sizeof(T), heap, capacity * sizeof(Item));
        return addr ? new (addr) T(capacity, data, count) : nullptr;
    }
    static T* create(const Item* data, size_t count, IN_MUT_HEAP) {
        return create(count, data, count, heap);
    }

    explicit Collection(heapsize capacity)
    :TypedObject<TYPE>(capacity * sizeof(Item)) { }

    Collection(size_t capacity, const Item *items, size_t count)
    :Collection(heapsize(capacity))
    {
        assert(count <= capacity);
        assert(capacity <= MaxCount);
        auto dst = begin();
        if (items)
            ::memcpy(dst, items, count * sizeof(Item));
        else
            count = 0;
        ::memset(dst + count, 0, (capacity - count) * sizeof(Item));
    }
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<String, char, Type::String> {
public:
    static String* create(string_view str, IN_MUT_HEAP) {
        return Collection::create(str.data(), str.size(), heap);
    }

    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit String(heapsize capacity)   :Collection(capacity) { }
    String(size_t cap, const char *str, size_t size) :Collection(cap, str, size) { }
};



/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<Blob, byte, Type::Blob> {
public:
    static Blob* create(size_t capacity, IN_MUT_HEAP) {
        return Collection::createUninitialized(capacity, heap);
    }
    static Blob* create(const void *data, size_t size, IN_MUT_HEAP) {
        return Collection::create((const byte*)data, size, heap);
    }

    slice<byte> bytes()             {return Collection::items();}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Blob(heapsize capacity)   :Collection(capacity) { }
    Blob(size_t cap, const byte *str, size_t size) :Collection(cap, str, size) { }
};



/// An array of `Val`s.
class Array : public Collection<Array, Val, Type::Array> {
public:
    static Array* create(heapsize count, IN_MUT_HEAP) {
        return Collection::create(nullptr, count, heap);
    }
    static Array* create(std::initializer_list<Val> vals, IN_MUT_HEAP) {
        return Collection::create(vals.begin(), vals.size(), heap);
    }
    static Array* create(slice<Val> vals, size_t capacity, IN_MUT_HEAP) {
        return Collection::create(capacity, vals.begin(), vals.size(), heap);
    }

    Val& operator[] (heapsize i)        {return items()[i];}
    Val  operator[] (heapsize i) const  {return items()[i];}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Array(heapsize capacity)    :Collection(capacity) { }
    Array(size_t cap, const Val *vals, size_t count) :Collection(cap, vals, count) { }
};



struct DictEntry {
    Val const key;
    Val       value;
};


/// A key-value mapping.
/// Keys can be anything but null, and are compared by identity (i.e. two Strings with the same
/// contents are not the same key!) Values can be anything including null.
class Dict : public Collection<Dict, DictEntry, Type::Dict> {
public:
    /// Creates an empty dictionary with the given capacity.
    static Dict* create(heapsize capacity, IN_MUT_HEAP) {
        return Collection::create(nullptr, capacity, heap);
    }
    /// Creates a dictionary from a list of key-value pairs. It will have no extra capacity.
    static Dict* create(std::initializer_list<DictEntry> vals, IN_MUT_HEAP) {
        return Collection::create(vals.begin(), vals.size(), heap);
    }
    /// Creates a dictionary from a list of key-value pairs.
    /// The capacity must be at least the number of pairs but can be larger.
    static Dict* create(std::initializer_list<DictEntry> vals, heapsize capacity, IN_MUT_HEAP) {
        void* addr = Object::alloc(sizeof(Dict), heap, capacity * sizeof(Val));
        return addr ? new (addr) Dict(capacity, vals.begin(), vals.size()) : nullptr;
    }

    heapsize capacity() const           {return Collection::capacity();}
    bool full() const                   {return capacity() == 0 || (endAll() - 1)->key != nullval;}
    bool empty() const                  {return capacity() == 0 || begin()->key == nullval;}
    heapsize count() const              {return heapsize(items().size());}
    size_t size() const                 {return count();}

    Val* find(Val key);
    const Val* find(Val key) const      {return const_cast<Dict*>(this)->find(key);}
    Val get(Val key) const              {auto v = find(key); return v ? *v : nullval;}
    bool contains(Val key) const        {return find(key) != nullptr;}

    bool set(Val key, Val newValue)     {return set(key, newValue, false);}
    bool insert(Val key, Val newValue)  {return set(key, newValue, true);}
    bool replace(Val key, Val newValue);
    bool remove(Val key);

    Val operator[] (Val key) const      {return get(key);}

    slice<DictEntry> items() const;

    iterator begin()                    {return items().begin();}
    iterator end()                      {return items().end();} // redefined items()
    const_iterator begin() const        {return const_cast<Dict*>(this)->begin();}
    const_iterator end() const          {return const_cast<Dict*>(this)->end();}

    static bool keyCmp(DictEntry const& a, DictEntry const& b) {return Val::keyCmp(a.key, b.key);}

private:
    friend GarbageCollector;
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Dict(heapsize capacity)   :Collection(capacity) { }

    Dict(size_t capacity, const DictEntry *ents, size_t count)
    :Collection(capacity, ents, count)
    {
        if (count > 0)
            sort(count);
    }

    Dict(const DictEntry *ents, size_t count)       :Dict(count, ents, count) { }

    slice<DictEntry> allItems() const               {return Collection::items();}
    void sort(size_t count);
    void sort()                                     {sort(capacity());}
    const_iterator endAll() const                   {return begin() + capacity();}
    bool set(Val key, Val value, bool insertOnly);
};



std::ostream& operator<<(std::ostream&, String const*);
std::ostream& operator<<(std::ostream&, Array const*);
static inline std::ostream& operator<<(std::ostream& out, String const& str) {return out << &str;}
static inline std::ostream& operator<<(std::ostream& out, Array const& arr) {return out << &arr;}



template <class T> void GarbageCollector::update(T** obj) {
    *obj = (T*)scan(*obj);
}
