//
// Collections.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Object.hh"


/// Abstract base of String, Array, Dict
template <class T, typename ITEM, Type TYPE>
class Collection : public TypedObjectRef<TYPE> {
public:
    using Item = ITEM;

    static constexpr heapsize MaxCount = (Block::MaxSize / sizeof(Item)) - 1;

    slice<Item> items()             {return this->template dataAs<Item>();}
    slice<Item> items() const       {return this->template dataAs<Item>();}

    heapsize capacity() const       {return items().size();}
    heapsize count() const          {return capacity();}        // Dict "overrides" this
    size_t size() const             {return count();}
    bool empty() const              {return count() == 0;}

    using iterator = Item*;
    using const_iterator = const Item*;

    const_iterator begin() const    {return items().begin();}
    const_iterator end() const      {return items().end();}
    iterator begin()                {return items().begin();}
    iterator end()                  {return items().end();}

protected:
    Collection(Val val, IN_HEAP)
    :TypedObjectRef<TYPE>(val, heap) { }

    Collection(size_t capacity, IN_MUT_HEAP)
    :TypedObjectRef<TYPE>(capacity * sizeof(ITEM), TYPE, heap) { }

    Collection(size_t capacity, const Item* items, size_t count, IN_MUT_HEAP)
    :Collection(capacity, heap)
    {
        assert(count <= capacity);
        if (items)
            ::memcpy(begin(), items, count * sizeof(Item));
        else
            count = 0;
        ::memset(begin() + count, 0, (capacity - count) * sizeof(Item));
    }

    Collection(const Item* items, size_t count, IN_MUT_HEAP)
    :Collection(count, items, count, heap) { }
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<String, char, Type::String> {
public:
    String(string_view str, IN_MUT_HEAP)
    :Collection(str.data(), str.size(), heap) { }

    const char* data() const        {return begin();}
    string_view get() const         {return {begin(), size()};}
};



/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<Blob, byte, Type::Blob> {
public:
    Blob(size_t capacity, IN_MUT_HEAP)
    :Collection(capacity, heap) { }

    Blob(const void *data, size_t size, IN_MUT_HEAP)
    :Collection((const byte*)data, size, heap) { }

    slice<byte> bytes()             {return items();}
};



/// An array of `Val`s.
class Array : public Collection<Array, Val, Type::Array> {
public:
    Array(heapsize count, IN_MUT_HEAP)
    :Collection(nullptr, count, heap) { }
    
    Array(std::initializer_list<Val> vals, IN_MUT_HEAP)
    :Collection(vals.begin(), vals.size(), heap) { }

    Array(slice<Val> vals, size_t capacity, IN_MUT_HEAP)
    :Collection(capacity, vals.begin(), vals.size(), heap) { }

    Val& operator[] (heapsize i)        {return items()[i];}
    Val  operator[] (heapsize i) const  {return items()[i];}
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
    Dict(heapsize capacity, IN_MUT_HEAP)
    :Collection(nullptr, capacity, heap) { }

    /// Creates a dictionary from a list of key-value pairs. It will have no extra capacity.
    Dict(std::initializer_list<DictEntry> vals, IN_MUT_HEAP)
    :Collection(vals.begin(), vals.size(), heap) { }

    /// Creates a dictionary from a list of key-value pairs.
    /// The capacity must be at least the number of pairs but can be larger.
    Dict(std::initializer_list<DictEntry> vals, heapsize capacity, IN_MUT_HEAP)
    :Collection(size_t(capacity), vals.begin(), vals.size(), heap) { }

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

    slice<DictEntry> allItems() const               {return Collection::items();}
    void sort(size_t count);
    void sort()                                     {sort(capacity());}
    const_iterator endAll() const                   {return begin() + capacity();}
    bool set(Val key, Val value, bool insertOnly);
};



std::ostream& operator<<(std::ostream&, String const&);
std::ostream& operator<<(std::ostream&, Array const&);


//template <class T> void GarbageCollector::update(T** obj) {
//    *obj = (T*)scan(*obj);
//}
