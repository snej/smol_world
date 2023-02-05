//
// Collections.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Value.hh"
#include <initializer_list>
#include <string_view>

namespace snej::smol {

using string_view = std::string_view;


/// Abstract base of String, Symbol, Blob, Array, Dict
template <typename ITEM, Type TYPE>
class Collection : public TypedObject<TYPE> {
public:
    using Item = ITEM;

    static constexpr heapsize MaxCount = (Block::MaxSize / sizeof(Item)) - 1;

    slice<Item> items()             {return slice_cast<Item>(this->rawBytes());}
    slice<Item> items() const       {return slice_cast<Item>(this->rawBytes());}

    heapsize capacity() const       {return items().size();}
    heapsize size() const           {return capacity();}        // Dict "overrides" this
    bool empty() const              {return size() == 0;}

    using iterator = Item*;
    using const_iterator = const Item*;

    const_iterator begin() const    {return items().begin();}
    const_iterator end() const      {return items().end();}
    iterator begin()                {return items().begin();}
    iterator end()                  {return items().end();}

protected:
    Collection() = default;

    Collection(Val const& val)      :TypedObject<TYPE>(val) { }

    Collection(Block *block, size_t capacity, const Item* items, size_t count)
    :TypedObject<TYPE>(block)
    {
        if (items)
            ::memcpy(begin(), items, count * sizeof(Item));
        else
            count = 0;
        ::memset(begin() + count, 0, (capacity - count) * sizeof(Item));
    }
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<char, Type::String> {
public:
    const char* data() const        {return begin();}
    string_view str() const         {return {begin(), size()};}
};

Maybe<String> newString(string_view str, Heap &heap);
Maybe<String> newString(const char *str, size_t length, Heap &heap);



/// A unique string: there is only one Symbol in a Heap with any specific string value.
/// The SymbolTable class manages Symbols.
class Symbol : public Collection<char, Type::Symbol> {
public:
    const char* data() const        {return begin();}
    string_view str() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    friend class SymbolTable;
    static Value create(string_view str, Heap &heap);
};

Maybe<Symbol> newSymbol(string_view str, Heap &heap);
Maybe<Symbol> newSymbol(const char *str, size_t length, Heap &heap);


/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<byte, Type::Blob> {
public:
    slice<byte> bytes()             {return items();}
};

Maybe<Blob> newBlob(size_t capacity, Heap &heap);
Maybe<Blob> newBlob(const void *data, size_t size, Heap &heap);


/// An array of `Val`s.
class Array : public Collection<Val, Type::Array> {
public:
    Val& operator[] (heapsize i)                {return items()[i];}
    Val const&  operator[] (heapsize i) const   {return items()[i];}

    heapsize count() const pure;
    bool full() const                           {return capacity() == 0 || end()[-1] != nullval;}
    bool insert(Value, heapsize pos);
    bool append(Value);
};

Maybe<Array> newArray(heapsize count, Heap &heap);
Maybe<Array> newArray(std::initializer_list<Val> vals, Heap &heap);
Maybe<Array> newArray(slice<Val> vals, size_t capacity, Heap &heap);


struct DictEntry {
    Val const key;
    Val       value;

    DictEntry() { };
    DictEntry(DictEntry&& other) {*this = std::move(other);}
    DictEntry& operator=(DictEntry &&);
};


/// A key-value mapping.
/// Keys can be anything but null, and are compared by identity (i.e. two Strings with the same
/// contents are not the same key!) Values can be anything including null.
class Dict : public Collection<DictEntry, Type::Dict> {
public:
    heapsize capacity() const           {return Collection::capacity();}
    bool full() const                   {return capacity() == 0 || (endAll() - 1)->key != nullval;}
    bool empty() const                  {return capacity() == 0 || begin()->key == nullval;}
    heapsize size() const               {return heapsize(items().size());} // "overridden"

    Val* find(Value key);
    const Val* find(Value key) const    {return const_cast<Dict*>(this)->find(key);}
    Value get(Value key) const          {auto v = find(key); return v ? Value(*v) : nullptr;}
    bool contains(Value key) const      {return find(key) != nullptr;}

    bool set(Value key, Value newValue)     {return set(key, newValue, false);}
    bool insert(Value key, Value newValue)  {return set(key, newValue, true);}
    bool replace(Value key, Value newValue);
    bool remove(Value key);

    Value operator[] (Value key) const  {return get(key);}

    slice<DictEntry> items() const;

    iterator begin()                    {return allItems().begin();}
    iterator end()                      {return items().end();} // redefined items()
    const_iterator begin() const        {return const_cast<Dict*>(this)->begin();}
    const_iterator end() const          {return const_cast<Dict*>(this)->end();}

    void dump(std::ostream& out) const;
    void dump() const;

    void sort(size_t count);
    void sort()                         {sort(capacity());}
    void postGC()                       {sort();}   // GC changes the ordering of pointers

private:
    slice<DictEntry> allItems() const               {return Collection::items();}
    const_iterator endAll() const                   {return begin() + capacity();}
    bool set(Value key, Value value, bool insertOnly);
};

Maybe<Dict> newDict(heapsize capacity, Heap &heap);
Maybe<Dict> newDict(std::initializer_list<DictEntry> vals, Heap &heap);
Maybe<Dict> newDict(std::initializer_list<DictEntry> vals, heapsize capacity, Heap &heap);



std::ostream& operator<<(std::ostream&, String const&);
std::ostream& operator<<(std::ostream&, Array const&);


template <typename FN>
bool Value::visit(FN fn) const {
    switch (type()) {
        case Type::Null:    fn(as<Null>()); break;
        case Type::Bool:    fn(as<Bool>()); break;
        case Type::Int:     fn(as<Int>()); break;
        case Type::String:  fn(as<String>()); break;
        case Type::Symbol:  fn(as<Symbol>()); break;
        case Type::Blob:    fn(as<Blob>()); break;
        case Type::Array:   fn(as<Array>()); break;
        case Type::Dict:    fn(as<Dict>()); break;
        default:            assert(false); return false;
    }
    return true;
}


template <ValueClass T> T Val::as() const {
    return Value(*this).as<T>();
}

template <ValueClass T> Maybe<T> Val::maybeAs() const {
    return Value(*this).maybeAs<T>();
}

}
