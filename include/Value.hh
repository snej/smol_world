//
// Value.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Block.hh"
#include "slice.hh"
#include "Val.hh"

namespace snej::smol {


/// Value is like Val, but has a full pointer to the object (if it's an object.)
/// Used in memory, never stored in the heap.
class Value : public ValBase<uintptr_t> {
public:
    constexpr Value()                               = default;
    constexpr Value(nullptr_t)                      :ValBase() { }
    constexpr explicit Value(bool b)                :ValBase(b) { }
    constexpr Value(int i)                          :ValBase(i) { }

    Value(Val const& val) {
        if (Block *block = val.block())
            setBlock(block);
        else
            _val = val.rawBits();
    }

    explicit Value(Block const* block)              {setBlock(block);}

    Type type() const pure;
    explicit operator bool() const pure             {return !isNull();}

    inline Object asObject() const pure;

    Block* block() const pure                       {return (isObject()) ? _block() : nullptr;}

    template <ValueClass T> bool is() const pure    {return T::HasType(type());}
    template <ValueClass T> T as() const pure       {auto x = T::fromValue(*this); return (T&)x;}
    template <ValueClass T> Maybe<T> maybeAs() const pure   {return Maybe<T>(*this);}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this value cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    friend bool operator== (Value const& a, Value const& b) pure     {return a._val == b._val;}
    friend bool operator!= (Value const& a, Value const& b) pure     {return a._val != b._val;}

    static Value fromValue(Value v) pure            {return v;}

private:
    friend class Val;
    friend class Object;
    
    Block* _block() const pure              {assert(isObject()); return (Block*)(_val >> TagSize);}
    void setBlock(Block const* block)       {_val = uintptr_t(block) << TagSize;}
};

constexpr Value nullvalue;

std::ostream& operator<< (std::ostream&, Value);



/// The Value subclass representing the Null type.
class Null : public Value {
public:
    static constexpr Type Type = Type::Null;
    constexpr static bool HasType(enum Type t)    {return t == Type;}

    constexpr Null() = default;
    constexpr Null(nullptr_t)                     :Value() { }
    constexpr explicit Null(Value v)              :Value(v) {assert(v.isNull());}
};


/// The Value subclass representing the Bool type.
class Bool : public Value {
public:
    static constexpr Type Type = Type::Bool;
    constexpr static bool HasType(enum Type t)    {return t == Type;}

    constexpr explicit Bool(bool b = false)       :Value(b) { }
    constexpr explicit Bool(Value v)              :Value(v) {assert(v.isBool());}
    constexpr explicit operator bool() const      {return asBool();}
};


/// The Value subclass representing the Int type.
class Int : public Value {
public:
    static constexpr Type Type = Type::Int;
    constexpr static bool HasType(enum Type t)            {return t == Type;}

    constexpr Int(int i = 0)                              :Value(i) { }
    constexpr explicit Int(Value v)                       :Value(v) {assert(v.isInt());}
    constexpr operator int() const                        {return asInt();}

    friend constexpr bool operator==(Int const& a, int b) {return a.asInt() == b;}
    friend constexpr bool operator==(int a, Int const& b) {return a == b.asInt();}
};



/// A reference to a heap object -- any type except Null, Bool or Int.
class Object {
public:
    constexpr static bool HasType(enum Type t)      {return t < Type::Null;}

    explicit Object(Block const* block)             :_data(block->data()) { }
    explicit Object(Val const& val)                 :Object(val._block()) { }
    explicit Object(Value val)               :Object(val._block()) { }

    operator Value() const pure                     {return Value(block());}

    bool isNull() const pure                        {return _data.isNull();}

    template <ObjectClass T> bool is() const pure   {return T::HasType(type());}
    template <ObjectClass T> T as() const pure      {assert(is<T>()); return *(T*)this;}
    template <ObjectClass T> Maybe<T> maybeAs() const pure {return Maybe<T>(*this);}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this value cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    Block* block() const pure                       {return Block::fromData(rawBytes());}
    Type type() const pure                          {return block()->type();}

    slice<byte> rawBytes() const pure               {return _data;}

    friend bool operator==(Object const& a, Object const& b) pure    {return a._data.begin() == b._data.begin();}
    friend bool operator!=(Object const& a, Object const& b) pure    {return !(a == b);}

    static Object fromValue(Value v) pure           {return Object(v);}

protected:
    friend class GarbageCollector;
    friend class Heap;
    friend class Val;
    template <ObjectClass T> friend class Maybe;

    Object() = default; // allows Maybe<> to hold a null; otherwise an illegal state
    template <ValueClass T> T _as() const pure      {return *(T*)this;}
    void relocate(Block* newBlock)                  {_data.moveTo((byte*)newBlock->dataPtr());}

private:
    slice<byte> _data;
};



/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObject : public Object {
public:
    static constexpr Type Type = TYPE;
    static bool HasType(enum Type t) {return t == Type;}
protected:
    explicit TypedObject(Block const* block)        :Object(block) { }
    explicit TypedObject(Val const& val)            :Object(val) {assert(type() == TYPE);}
};



/// A `std::optional`-like type for Object classes.
template <ObjectClass T>
class Maybe {
public:
    Maybe() = default;
    Maybe(nullptr_t)                    :Maybe() { }
    explicit Maybe(Value val)    {if (T::HasType(val.type())) _obj = Object(val);}
    explicit Maybe(Block *block)        :_obj(block) {assert(_obj.type() == T::Type);}
    Maybe(T const& obj)                 :_obj(obj) { }

    explicit operator bool() const      {return !_obj.isNull();}
    operator Value() const              {return _obj.isNull() ? nullvalue : Value(_obj);}

    T& value()                          {return *getp();}
    T const& value() const              {return const_cast<Maybe*>(this)->get();}

    // do not call this directly! It's only for use by MAYBE()
    friend T _unsafeval_(Maybe<T> const& m)     {return reinterpret_cast<T const&>(m._obj);}

    friend std::ostream& operator<< (std::ostream& o, Maybe const& m) {return o << m._obj;}

    friend bool operator==(Maybe<T> const& a, Maybe<T> const& b) {return a._obj == b._obj;}
    friend bool operator==(Maybe<T> const& a, T const& b) {return a._obj == b;}
    friend bool operator==(T const& a, Maybe<T> const& b) {return b._obj == a;}
private:
    T* getp()                           {assert(!_obj.isNull()); return reinterpret_cast<T*>(&_obj);}

    Object _obj;
};


#define if_let(VAR, EXPR)  if (auto VAR = _unsafeval_(EXPR); !VAR.isNull())
#define unless(VAR, EXPR)  auto VAR = _unsafeval_(EXPR); if (VAR.isNull())


inline Object Value::asObject() const                         {return Object(*this);}

}
