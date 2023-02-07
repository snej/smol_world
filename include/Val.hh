//
// Val.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.hh"
#include <iosfwd>
#include <type_traits>

namespace snej::smol {

class Block;
class Object;
class Value;


/** Value types. */
enum class Type : uint8_t {
    // Object types:  (these come from Block type tags, 0..15)
    Float = 0,
    BigInt,
    String,
    Symbol,
    Blob,
    Array,
    Vector,
    Dict,
    // (8 spares)

    // Primitives:  (these are stored inline in a Val without any pointers)
    Null = 0x10,
    Bool,
    Int,

    Max = Int,
}; // Note: If you change this you must update TypeSet::All below and kTypeNames[] in Val.cc

const char* TypeName(Type t);

std::ostream& operator<<(std::ostream& out, Type);

constexpr uint32_t _mask(Type t) {return uint32_t(1) << uint8_t(t);}

enum class TypeSet : uint32_t {
    Object      = 0b00000000011111111,
    Inline      = _mask(Type::Null)  | _mask(Type::Bool)   | _mask(Type::Int),
    Numeric     = _mask(Type::Int)   | _mask(Type::BigInt) | _mask(Type::Float),
    Container   = _mask(Type::Array) | _mask(Type::Vector) | _mask(Type::Dict),
    Valid       = uint32_t(Object) | uint32_t(Inline),
};

constexpr bool TypeIs(Type t, TypeSet set) {return (_mask(t) & uint32_t(set)) != 0;}


// template class with common functionality between Val and Value. Don't use directly.
template <typename RAWVAL>
class ValBase {
public:
    constexpr ValBase()                         :_val(NullVal) { }
    constexpr explicit ValBase(nullptr_t)       :ValBase() { }
    constexpr explicit ValBase(int i)           :_val(encodeInt(i)) { }

    constexpr bool isNull() const               {return _val == NullVal;}
    constexpr bool isNullish() const            {return _val == NullishVal;}
    constexpr bool isBool() const               {return _val == FalseVal || _val == TrueVal;}
    constexpr bool asBool() const               {return _val > FalseVal;}
    constexpr bool isInt() const                {return (_val & IntTag) != 0;}
    constexpr int asInt() const                 {assert(isInt()); return int32_t(_val) >> TagSize;}
    constexpr bool isObject() const             {return (_val & IntTag) == 0 && _val > TrueVal;}

    /// A Val/Value is "truthy" if it is not `null`. (Yes, `nullish` is truthy.)
    constexpr explicit operator bool() const pure {return !isNull();}

protected:
    friend class Value;

    enum TagBits : uint32_t {
        IntTag      = 0b001,
    };

    static constexpr int TagSize = 1;

    static constexpr RAWVAL encodeInt(int i)    {return (i << TagSize) | IntTag;}

    enum magic : RAWVAL {
        NullVal    = 0,
        NullishVal = 2,
        FalseVal   = 4,
        TrueVal    = 6,
    };

    constexpr explicit ValBase(magic m)         :_val(m) { }

    RAWVAL _val;
};



/// A 32-bit polymorphic data value inside a Heap.
/// Can be null, boolean, an integer, or a reference to an object in the heap.
/// Application code never creates values of this type; use `Value` instead.
class Val : public ValBase<uintpos> {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val() = default;

    Val& operator= (Val const&);
    Val& operator= (Value);

    Type type() const pure;

    /// True if the value has a numeric type (Int, BigInt or Float.)
    bool isNumber() const pure                          {return TypeIs(type(), TypeSet::Numeric);}

    /// Returns the value as a number. Unlike `asInt` this supports types other than Int;
    /// it supports Bool, Int, BigInt and Float, and otherwise returns 0.
    template <Numeric NUM> NUM asNumber() const pure;

    /// Returns the value as an Object. Same as `Object(val)`.
    /// Only legal if `isObject()` returns true.
    Object asObject() const pure;

    template <ValueClass T> bool is() const pure                {return T::HasType(type());}

    template <ValueClass T> T as() const pure;
    template <ValueClass T> Maybe<T> maybeAs() const pure;

    friend constexpr bool operator== (Val const& a, Val const& b) pure    {return a._val == b._val;}
    friend constexpr bool operator==(Val const& v, int i) pure      {return v._val == encodeInt(i);}

    friend void swap(Val const&, Val const&);

    //---- Blocks:

    Val& operator= (Block const* dst);

    Block* block() const pure                           {return isObject() ? _block() : nullptr;}

    Block* _block() const pure {
        // Convert signed offset back to real pointer:
        assert(isObject());
        return (Block*)(intptr_t(this) + (int32_t(_val) >> TagSize));
    }

    //---- Ignore this

    // Yes, the copy constructor is illegal! We cannot allow Val objects to be created on the
    // stack, since the stack may well be > 2GB away from the target address. That means Val cannot
    // be passed by value or returned from a function or used as a local variable.
    Val(Val const&) = delete;
    // don't use this, use `nullishval` instead
    static constexpr Val _nullish()                             {return Val(ValBase::NullishVal);}
private:
    constexpr explicit Val(magic m)           :ValBase(m) { }
};

static constexpr Val nullval;

/// A Val whose type is Null but isn't the same as nullval. Used for JSON `null`.
static constexpr Val nullishval = Val::_nullish();

}
