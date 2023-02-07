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


/// Abstract base of String, Symbol, Blob, Array, Dict
template <typename ITEM, Type TYPE>
class Collection : public Object {
public:
    using Item = ITEM;
    static constexpr Type Type = TYPE;
    static bool HasType(enum Type t) {return t == Type;}

    static constexpr heapsize MaxCount = (Block::MaxSize / sizeof(Item)) - 1;

    slice<Item> items()             {return slice_cast<Item>(this->rawBytes());}// Vector & Dict
    slice<Item> items() const       {return slice_cast<Item>(this->rawBytes());}// "override" these

    heapsize capacity() const       {return items().size();}
    heapsize size() const           {return capacity();}        // Vector & Dict "override" this
    bool empty() const              {return size() == 0;}

    using iterator = Item*;
    iterator begin()                {return items().begin();}
    iterator end()                  {return items().end();}

    using const_iterator = const Item*;
    const_iterator begin() const    {return items().begin();}
    const_iterator end() const      {return items().end();}

    Collection() = delete; // (these aren't constructed. See newObject() in Heap.cc)
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<char, Type::String> {
public:
    const char* data() const        {return begin();}
    std::string_view str() const    {return {begin(), size()};}
};

Maybe<String> newString(std::string_view str, Heap &heap);
Maybe<String> newString(const char *str, size_t length, Heap &heap);



/// A unique string: there is only one Symbol in a Heap with any specific string value.
/// The SymbolTable class manages Symbols.
class Symbol : public String {
public:
    static constexpr enum Type Type = Type::Symbol;
    static bool HasType(enum Type t) {return t == Type;}

private:
    friend class SymbolTable;
    static Value create(std::string_view str, Heap &heap);
};

Maybe<Symbol> newSymbol(std::string_view str, Heap &heap);
Maybe<Symbol> newSymbol(const char *str, size_t length, Heap &heap);


/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<byte, Type::Blob> {
public:
    slice<byte> bytes() const                   {return items();}
};

Maybe<Blob> newBlob(size_t capacity, Heap &heap);
Maybe<Blob> newBlob(const void *data, size_t size, Heap &heap);


/// A fixed-size array of `Val`s.
class Array : public Collection<Val, Type::Array> {
public:
    Val& operator[] (heapsize i)                {return items()[i];}
    Val const&  operator[] (heapsize i) const   {return items()[i];}
};

Maybe<Array> newArray(heapsize size, Heap &heap);
Maybe<Array> newArray(heapsize size, Value initialValue, Heap &heap);
Maybe<Array> newArray(slice<Val> vals, size_t capacity, Heap &heap);



/// A variable-size array of `Val`s. Uses an additional slot to store the current size.
class Vector : public Collection<Val, Type::Vector> {
public:
    heapsize capacity() const pure              {return Collection::capacity() - 1;}
    heapsize size() const pure                  {return Collection::begin()->asInt();}
    bool empty() const                          {return size() == 0;}
    bool full() const                           {return size() == capacity();}

    iterator begin()                            {return Collection::begin() + 1;}
    iterator end()                              {return Collection::end();}
    const_iterator begin() const                {return Collection::begin() + 1;}
    const_iterator end() const                  {return Collection::end();}

    slice<Val> items() const                    {return {(Val*)begin(), size()};}
    Val& operator[] (heapsize i)                {return Collection::items()[i + 1];}
    Val const&  operator[] (heapsize i) const   {return Collection::items()[i + 1];}

    bool insert(Value, heapsize pos);
    bool append(Value);
    void clear()                                {_setSize(0);}
    
private:
    slice<Val> allItems() const                 {return Collection::items();}
    void _setSize(heapsize);
};

Maybe<Vector> newVector(heapsize capacity, Heap &heap);
Maybe<Vector> newVector(slice<Val> vals, size_t capacity, Heap &heap);


struct DictEntry {
    Val const key;      // always a Symbol. Immutable.
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

    Val* find(Symbol key);
    const Val* find(Symbol key) const    {return const_cast<Dict*>(this)->find(key);}
    Value get(Symbol key) const          {auto v = find(key); return v ? Value(*v) : nullptr;}
    bool contains(Symbol key) const      {return find(key) != nullptr;}

    bool set(Symbol key, Value newValue)     {return set(key, newValue, false);}
    bool insert(Symbol key, Value newValue)  {return set(key, newValue, true);}
    bool replace(Symbol key, Value newValue);
    bool remove(Symbol key);

    Value operator[] (Symbol key) const  {return get(key);}

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
    bool set(Symbol key, Value value, bool insertOnly);
};

Maybe<Dict> newDict(heapsize capacity, Heap &heap);



std::ostream& operator<<(std::ostream&, String const&);
std::ostream& operator<<(std::ostream&, Array const&);


template <typename FN>
bool Value::visit(FN fn) const {
    switch (type()) {
        case Type::Null:    fn(as<Null>()); break;
        case Type::Bool:    fn(as<Bool>()); break;
        case Type::Int:     fn(as<Int>()); break;
        case Type::BigInt:  fn(as<BigInt>()); break;
        case Type::Float:   fn(as<Float>()); break;
        case Type::String:  fn(as<String>()); break;
        case Type::Symbol:  fn(as<Symbol>()); break;
        case Type::Blob:    fn(as<Blob>()); break;
        case Type::Array:   fn(as<Array>()); break;
        case Type::Vector:  fn(as<Vector>()); break;
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
