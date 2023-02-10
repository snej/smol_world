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


/// Abstract base of String, Symbol, Blob, Array, Vector, Dict.
template <typename ITEM, Type TYPE, class Subclass>
class Collection : public Object {
public:
    using Item = ITEM;
    static constexpr Type Type = TYPE;
    static bool HasType(enum Type t) {return t == Type;}

    static constexpr heapsize MaxCount = (Block::MaxSize / sizeof(Item)) - 1;

    // These are "mixin" methods that call and depend on the _subclass's_ implementation of
    // `items`.
    // The `capacity` method defaults to returning the objects' entire size, but can be overridden.

    heapsize capacity() const                   {return _capacity();}
    heapsize size() const                       {return ((Subclass*)this)->items().size();}
    bool empty() const                          {return size() == 0;}
    bool full() const                           {return size() == ((Subclass*)this)->capacity();}

    using iterator = Item*;
    iterator begin()                            {return ((Subclass*)this)->items().begin();}
    iterator end()                              {return ((Subclass*)this)->items().end();}

    using const_iterator = const Item*;
    const_iterator begin() const                {return ((Subclass*)this)->items().begin();}
    const_iterator end() const                  {return ((Subclass*)this)->items().end();}

    Item& operator[] (heapsize i)               {return ((Subclass*)this)->items()[i];}
    Item const& operator[] (heapsize i) const   {return ((Subclass*)this)->items()[i];}

protected:
    Collection() = delete; // (these aren't constructed. See newObject() in Heap.cc)

    // Default versions that assume the entire object capacity is in use:
    heapsize _capacity() const                  {return _items().size();}
    slice<Item> _items()                        {return slice_cast<Item>(this->rawBytes());}
    slice<Item> _items() const                  {return slice_cast<Item>(this->rawBytes());}
};



/// A string object. Stores UTF-8 characters. Not zero-terminated.
class String : public Collection<char, Type::String, String> {
public:
    std::string_view str() const pure           {return {begin(), size()};}

    slice<char> items()                         {return _items();}
    slice<char> items() const                   {return const_cast<String*>(this)->items();}
};

Maybe<String> newString(std::string_view str, Heap &heap);



/// A unique identifier. Has a string and a 16-bit integer, both unique in this Heap.
/// The SymbolTable class manages Symbols.
class Symbol : public Collection<char, Type::Symbol, Symbol> {
public:
    enum class ID : uint16_t { };

    ID id() const                               {return *(ID*)rawBytes().begin();}

    std::string_view str() const pure           {return {begin(), size()};}

    inline slice<char> items()                  {return _items().moveStart(sizeof(ID));}
    slice<char> items() const                   {return const_cast<Symbol*>(this)->items();}

private:
    friend class SymbolTable;
    static Value create(ID, std::string_view str, Heap &heap); // only SymbolTable calls this
};

Maybe<Symbol> newSymbol(std::string_view str, Heap &heap);



/// A blob object ... just like a String but with `byte` instead of `char`.
class Blob : public Collection<byte, Type::Blob, Blob> {
public:
    slice<byte> bytes() const                   {return items();}

    slice<byte> items()                         {return _items();}
    slice<byte> items() const                   {return const_cast<Blob*>(this)->items();}
};

Maybe<Blob> newBlob(size_t capacity, Heap &heap);
Maybe<Blob> newBlob(const void *data, size_t size, Heap &heap);


/// A fixed-size array of `Val`s.
class Array : public Collection<Val, Type::Array, Array> {
public:
    slice<Val> items()                          {return _items();}
    slice<Val> items() const                    {return const_cast<Array*>(this)->items();}
};

Maybe<Array> newArray(heapsize size, Heap &heap);
Maybe<Array> newArray(heapsize size, Value initialValue, Heap &heap);
Maybe<Array> newArray(slice<Val> vals, size_t capacity, Heap &heap);



/// A variable-size array of `Val`s. Uses an additional slot to store the current size.
class Vector : public Collection<Val, Type::Vector, Vector> {
public:
    heapsize capacity() const pure              {return _capacity() - 1;}
    heapsize size() const pure                  {return _items()[0].asInt();}

    slice<Val> items()                          {return {_items().begin() + 1, size()};}
    slice<Val> items() const                    {return const_cast<Vector*>(this)->items();}

    bool insert(Value, heapsize pos);
    bool append(Value);
    void clear()                                {_setSize(0);}

private:
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
class Dict : public Collection<DictEntry, Type::Dict, Dict> {
public:
    bool full() const                           {return capacity() == 0 || _items().end()[-1].key;}
    bool empty() const                          {return capacity() == 0 || begin()->key == nullval;}

    slice<DictEntry> items();
    slice<DictEntry> items() const              {return const_cast<Dict*>(this)->items();}

    Val* find(Symbol key);
    const Val* find(Symbol key) const           {return const_cast<Dict*>(this)->find(key);}
    Value get(Symbol key) const                 {auto v = find(key); return v ? Value(*v) :nullptr;}
    bool contains(Symbol key) const             {return find(key) != nullptr;}

    bool set(Symbol key, Value newValue)        {return set(key, newValue, false);}
    bool insert(Symbol key, Value newValue)     {return set(key, newValue, true);}
    bool replace(Symbol key, Value newValue);
    bool remove(Symbol key);

    Value operator[] (Symbol key) const         {return get(key);}

    void dump(std::ostream& out) const;
    void dump() const;

    void sort(size_t count);
    void sort()                                 {sort(capacity());}
    void postGC()                               {sort();}   // GC changes the ordering of pointers

private:
    bool set(Symbol key, Value value, bool insertOnly);
};

Maybe<Dict> newDict(heapsize capacity, Heap &heap);



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
