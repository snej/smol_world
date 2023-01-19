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

protected:
    static T* createUninitialized(heapsize capacity, IN_MUT_HEAP) {
        void* addr = Object::alloc(sizeof(T), heap, capacity * sizeof(Item));
        return addr ? new (addr) T(capacity) : nullptr;
    }
    static T* create(const Item* data, size_t count, IN_MUT_HEAP) {
        void* addr = Object::alloc(sizeof(T), heap, count * sizeof(Item));
        return addr ? new (addr) T(data, count) : nullptr;
    }

    explicit Collection(heapsize capacity)
    :TypedObject<TYPE>(capacity * sizeof(Item)) { }

    Collection(const Item *items, size_t count)
    :Collection(heapsize(count))
    {
        assert(count <= MaxCount);
        size_t size = count * sizeof(Item);
        if (items)
            ::memcpy(this->dataPtr(), items, size);
        else
            ::memset(this->dataPtr(), 0, size);
    }

    slice<Item> items()             {return this->template data<Item>();}
    slice<Item> items() const       {return const_cast<Collection*>(this)->items();}

    iterator begin()                {return items().begin();}
    iterator end()                  {return items().end();}
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<String, char, Type::String> {
public:
    static String* create(const char *str, size_t size, IN_MUT_HEAP) {
        return Collection::create(str, size, heap);
    }

    static String* create(string_view str, IN_MUT_HEAP) {
        return create(str.data(), str.size(), heap);
    }

    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit String(heapsize capacity)   :Collection(capacity) { }
    String(const char *str, size_t size) :Collection(str, size) { }
};



/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<Blob, byte, Type::Blob> {
public:
    static Blob* create(const void *data, size_t size, IN_MUT_HEAP) {
        return Collection::create((const byte*)data, size, heap);
    }

    slice<byte> bytes()             {return Collection::items();}
    const_iterator begin() const    {return Collection::begin();}
    const_iterator end() const      {return Collection::end();}
    iterator begin()                {return Collection::begin();}
    iterator end()                  {return Collection::end();}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Blob(heapsize capacity)   :Collection(capacity) { }
    Blob(const byte *str, size_t size) :Collection(str, size) { }
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

    iterator begin()                {return Collection::begin();}
    iterator end()                  {return Collection::end();}
    const_iterator begin() const    {return Collection::begin();}
    const_iterator end() const      {return Collection::end();}

    Val& operator[] (heapsize i)        {return items()[i];}
    Val  operator[] (heapsize i) const  {return items()[i];}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Array(heapsize capacity)    :Collection(capacity) { }
    Array(const Val *vals, size_t count) :Collection(vals, count) { }
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
        return addr ? new (addr) Dict(vals.begin(), vals.size(), capacity) : nullptr;
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

    slice<DictEntry> items();

    iterator begin()                    {return items().begin();}
    iterator end()                      {return items().end();}
    const_iterator begin() const        {return const_cast<Dict*>(this)->begin();}
    const_iterator end() const          {return const_cast<Dict*>(this)->end();}

    static bool keyCmp(DictEntry const& a, DictEntry const& b) {return Val::keyCmp(a.key, b.key);}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Dict(heapsize capacity)   :Collection(capacity) { }

    Dict(const DictEntry *ents, size_t count, heapsize capacity)
    :Collection(nullptr, capacity)
    {
        assert(capacity >= count);
        if (count > 0) {
            ::memcpy(begin(), ents, count * sizeof(DictEntry));
            sort(count);
        }
    }

    Dict(const DictEntry *ents, size_t count)       :Dict(ents, count, heapsize(count)) { }

    slice<DictEntry> allItems() const               {return Collection::items();}
    void sort(size_t count);
    iterator endAll()                               {return begin() + capacity();}
};



std::ostream& operator<<(std::ostream&, String const*);
std::ostream& operator<<(std::ostream&, Array const*);
static inline std::ostream& operator<<(std::ostream& out, String const& str) {return out << &str;}
static inline std::ostream& operator<<(std::ostream& out, Array const& arr) {return out << &arr;}



template <class T> void GarbageCollector::update(T** obj) {
    *obj = (T*)scan(*obj);
}
