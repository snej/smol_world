//
// Collections.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Object.hh"
#include <initializer_list>
#include <string_view>

using string_view = std::string_view;


/// Abstract base of String, Symbol, Blob, Array, Dict
template <typename ITEM, Type TYPE>
class Collection : public TypedObject<TYPE> {
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
    Collection() = default;

    Collection(Val val, IN_HEAP)
    :TypedObject<TYPE>(val, heap) { }

    Collection(Block *block, IN_MUT_HEAP, size_t capacity, const Item* items, size_t count)
    :TypedObject<TYPE>(block, heap)
    {
        if (items)
            ::memcpy(begin(), items, count * sizeof(Item));
        else
            count = 0;
        ::memset(begin() + count, 0, (capacity - count) * sizeof(Item));
    }

    static Value _create(size_t capacity, const Item* items, size_t count, IN_MUT_HEAP) {
        assert(count <= capacity);
        Block *block = Block::alloc(capacity * sizeof(Item), TYPE, heap);
        if (!block)
            return nullptr;
        return Collection(block, heap, capacity, items, count);
    }

    static Value _create(const Item* items, size_t count, IN_MUT_HEAP) {
        return _create(count, items, count, heap);
    }
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<char, Type::String> {
public:
    static Maybe<String> create(string_view str, IN_MUT_HEAP) {
        return Maybe<String>(_create(str.size(), str.data(), str.size(), heap));
    }

    const char* data() const        {return begin();}
    string_view get() const         {return {begin(), size()};}
};




/// A unique string: there is only one Symbol in a Heap with any specific string value.
/// Use SymbolTable to create and look up Symbols.
class Symbol : public Collection<char, Type::Symbol> {
public:
    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    friend class SymbolTable;
    static Maybe<Symbol> create(string_view str, IN_MUT_HEAP) {
        return Maybe<Symbol>(_create(str.size(), str.data(), str.size(), heap));
    }
};



/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<byte, Type::Blob> {
public:
    static Maybe<Blob> create(size_t capacity, IN_MUT_HEAP) {
        return Maybe<Blob>(_create(nullptr, capacity, heap));
    }

    static Maybe<Blob> create(const void *data, size_t size, IN_MUT_HEAP) {
        return Maybe<Blob>(_create((byte*)data, size, heap));
    }

    slice<byte> bytes()             {return items();}
};



/// An array of `Val`s.
class Array : public Collection<Val, Type::Array> {
public:
    static Maybe<Array> create(heapsize count, IN_MUT_HEAP) {
        return Maybe<Array>(_create(nullptr, count, heap));
    }
    
    static Maybe<Array> create(std::initializer_list<Val> vals, IN_MUT_HEAP) {
        return Maybe<Array>(_create(vals.begin(), vals.size(), heap));
    }

    static Maybe<Array> create(slice<Val> vals, size_t capacity, IN_MUT_HEAP) {
        return Maybe<Array>(_create(capacity, vals.begin(), vals.size(), heap));
    }

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
class Dict : public Collection<DictEntry, Type::Dict> {
public:
    /// Creates an empty dictionary with the given capacity.
    static Maybe<Dict> create(heapsize capacity, IN_MUT_HEAP) {
        return Maybe<Dict>(_create(capacity, nullptr, capacity, heap));
    }

    /// Creates a dictionary from a list of key-value pairs. It will have no extra capacity.
    static Maybe<Dict> create(std::initializer_list<DictEntry> vals, IN_MUT_HEAP) {
        return Maybe<Dict>(_create(vals.size(), vals.begin(), vals.size(), heap));
        // FIXME: Sort!
    }

    /// Creates a dictionary from a list of key-value pairs.
    /// The capacity must be at least the number of pairs but can be larger.
    static Maybe<Dict> create(std::initializer_list<DictEntry> vals, heapsize capacity, IN_MUT_HEAP) {
        return Maybe<Dict>(_create(size_t(capacity), vals.begin(), vals.size(), heap));
        // FIXME: Sort!
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
    slice<DictEntry> allItems() const               {return Collection::items();}
    void sort(size_t count);
    void sort()                                     {sort(capacity());}
    const_iterator endAll() const                   {return begin() + capacity();}
    bool set(Val key, Val value, bool insertOnly);
};



std::ostream& operator<<(std::ostream&, String const&);
std::ostream& operator<<(std::ostream&, Array const&);


template <typename FN>
bool Value::visit(FN fn) const {
    if (!*this)
        return false;
    switch (type()) {
        case Type::Null:    fn(as<NullValue>()); break;
        case Type::Int:     fn(as<IntValue>()); break;
        case Type::String:  fn(as<String>()); break;
        case Type::Symbol:  fn(as<Symbol>()); break;
        case Type::Blob:    fn(as<Blob>()); break;
        case Type::Array:   fn(as<Array>()); break;
        case Type::Dict:    fn(as<Dict>()); break;
        default:            return false;
    }
    return true;
}


template <ValueClass T> T Val::as(IN_HEAP) const {
    return Value(*this, heap).as<T>();
}

template <ValueClass T> Maybe<T> Val::maybeAs(IN_HEAP) const {
    return Value(*this, heap).maybeAs<T>();
}
