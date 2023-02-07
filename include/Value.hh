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

    /// True if the value has a numeric type (Int, BigInt or Float.)
    bool isNumber() const pure                      {return TypeIs(type(), TypeSet::Numeric);}

    /// Returns the value as a number. Unlike `asInt` this supports types other than Int;
    /// it supports Bool, Int, BigInt and Float, and otherwise returns 0.
    template <Numeric NUM> NUM asNumber() const pure;

    inline Object asObject() const pure;

    Block* block() const pure                       {return (isObject()) ? _block() : nullptr;}

    template <ValueClass T> bool is() const pure    {return T::HasType(type());}
    template <ValueClass T> T as() const pure       {auto x = T::fromValue(*this); return (T&)x;}
    template <ValueClass T> Maybe<T> maybeAs() const pure   {return Maybe<T>(*this);}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this value cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    friend bool operator== (Value const& a, Value const& b) pure     {return a._val == b._val;}

    static Value fromValue(Value v) pure            {return v;}

private:
    friend class Val;
    friend class Object;

    constexpr explicit Value(magic m)       :ValBase(m) { }
    Block* _block() const pure              {assert(isObject()); return (Block*)(_val >> TagSize);}
    void setBlock(Block const* block)       {_val = uintptr_t(block) << TagSize;}
};

std::ostream& operator<< (std::ostream&, Value);



/// The Value subclass representing the Null type.
class Null : public Value {
public:
    static constexpr Type Type = Type::Null;
    constexpr static bool HasType(enum Type t)    {return t == Type;}

    constexpr Null() = default;
    constexpr Null(nullptr_t)                       :Value() { }
    constexpr explicit Null(Value v)                :Value(v) {assert(v.isNull());}

    static constexpr Null _nullish()                {return Null(NullishVal);}
private:
    constexpr explicit Null(magic m)                {_val = m;}
};

constexpr Null nullvalue;

constexpr Null nullishvalue = Null::_nullish();


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

    static constexpr int Min = Val::MinInt;
    static constexpr int Max = Val::MaxInt;

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
    bool isNumber() const pure                      {return TypeIs(type(), TypeSet::Numeric);}

    /// Returns the object's numeric value if it's a BigInt or Float; else 0.
    template <Numeric NUM> NUM asNumber() const pure {return Value(*this).asNumber<NUM>();}

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

    static Object fromValue(Value v) pure           {return Object(v);}

protected:
    friend class GarbageCollector;
    friend class Heap;
    friend class Val;
    template <ObjectClass T> friend class Maybe;

    Object() = default; // allows Maybe<> to hold a null; otherwise an illegal state
    template <ValueClass T> T _as() const pure      {return *(T*)this;}
    void relocate(Block* newBlock)                  {_data = newBlock->data();}

private:
    slice<byte> _data;
};



/// The Object subclass representing the BigInt type.
class BigInt : public Object {
public:
    static constexpr Type Type = Type::BigInt;
    constexpr static bool HasType(enum Type t)      {return t == Type;}

    int64_t asInt() const {
        slice<byte> bytes = rawBytes();
        assert(bytes.size() > 0 && bytes.size() <= sizeof(int64_t));
        int64_t result = (uint8_t(bytes.back()) & 0x80) ? -1 : 0;   // sign-extend it
        memcpy(&result, rawBytes().begin(), rawBytes().size());
        return result;
    }

    operator int64_t() const                        {return asInt();}
};

/// Creates a new BigInt object.
Maybe<BigInt> newBigInt(int64_t, Heap&);

/// Returns a (non-object) Int if possible, else creates a BigInt.
Value newInt(int64_t i, Heap &heap);



/// The Object subclass representing the Float type.
class Float : public Object {
public:
    static constexpr Type Type = Type::Float;
    constexpr static bool HasType(enum Type t)      {return t == Type;}

    bool isDouble() const                           {return rawBytes().size() == sizeof(double);}
    float asFloat() const                           {return getValue<float>();}
    double asDouble() const                         {return getValue<double>();}
    operator float() const                          {return getValue<float>();}
    operator double() const                         {return getValue<double>();}

private:
    template <typename F> F getValue() const {
        if (rawBytes().size() == sizeof(float))
            return *(float*)rawBytes().begin();
        else {
            assert(rawBytes().size() == sizeof(double));
            return *(double*)rawBytes().begin();
        }
    }
};

/// Creates a new 4-byte Float object.
Maybe<Float> newFloat(float, Heap&);
/// Creates a new 8-byte Float object, or 4 bytes if possible without losing accuracy.
Maybe<Float> newFloat(double, Heap&);

/// Returns a (non-object) Int value if possible;
/// else creates a BigInt if the value is integral;
/// else creates a Float.
Value newNumber(double, Heap&);


    
/// A `std::optional`-like type for Object classes.
template <ObjectClass T>
class Maybe {
public:
    Maybe() = default;
    Maybe(nullptr_t)                    :Maybe() { }
    Maybe(Null)                         :Maybe() { }
    explicit Maybe(Value val)           {if (T::HasType(val.type())) _obj = Object(val);}
    explicit Maybe(Block *block)        :_obj(block) {assert(_obj.type() == T::Type);}
    Maybe(T const& obj)                 :_obj(obj) { }

    explicit operator bool() const      {return !_obj.isNull();}
    operator Value() const              {return _obj.isNull() ? Value(nullvalue) : Value(_obj);}

    T& value()                          {return *getp();}
    T const& value() const              {return const_cast<Maybe*>(this)->value();}

    // do not call this directly! It's only for use by MAYBE()
    friend T _unsafeval_(Maybe<T> const& m)     {return reinterpret_cast<T const&>(m._obj);}
    friend T& _unsafeval_(Maybe<T>& m)          {return reinterpret_cast<T&>(m._obj);}

    friend std::ostream& operator<< (std::ostream& o, Maybe const& m) {
        if (m) o << m._obj; else o << nullvalue; return o;
    }

    friend bool operator==(Maybe<T> const& a, Maybe<T> const& b)  {return a._obj == b._obj;}
    friend bool operator==(Maybe<T> const& a, Value b)  {return Value(a) == b;}
    friend bool operator==(Maybe<T> const& a, T const& b) {return a._obj == b;}
protected:
    T* getp()                           {assert(!_obj.isNull()); return reinterpret_cast<T*>(&_obj);}

    Object _obj;
};


#define if_let(VAR, EXPR)  if (auto VAR = _unsafeval_(EXPR); !VAR.isNull())
#define unless(VAR, EXPR)  auto VAR = _unsafeval_(EXPR); if (VAR.isNull())


inline Object Value::asObject() const                         {return Object(*this);}



template <Numeric NUM> NUM Val::asNumber() const {
    return Value(*this).asNumber<NUM>();
}

template <Numeric NUM> NUM Value::asNumber() const {
    switch (type()) {
        case Type::Bool:    return NUM(asBool());
        case Type::Int:     return pinning_cast<NUM>(asInt());
        case Type::BigInt:  return pinning_cast<NUM>(as<BigInt>().asInt());
        case Type::Float:   return pinning_cast<NUM>(as<Float>().asDouble());
        default:            return 0;
    }
    // TODO: Pin result to limit of type
}

}
