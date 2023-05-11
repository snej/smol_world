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
    Null    = 0,
    Int     = 1,
    Float   = 2,
    String  = 3,
    Symbol  = 4,
    Blob    = 5,
    Array   = 6,
    Dict    = 7,

    Bool    = 8,    // not actually stored in tag; encoded as Null tag

    Max = Bool,
}; // Note: If you change this you must update TypeSet::All below and kTypeNames[] in Val.cc

const char* TypeName(Type t);

std::ostream& operator<<(std::ostream& out, Type);

constexpr uint32_t _mask(Type t) {return uint32_t(1) << uint8_t(t);}

enum class TypeSet : uint32_t {
    Object      = 0b011111100,
    Inline      = _mask(Type::Null)  | _mask(Type::Bool)   | _mask(Type::Int),
    Numeric     = _mask(Type::Int)   | _mask(Type::Float),
    Container   = _mask(Type::Array) | _mask(Type::Dict),
    Valid       = uint32_t(Object) | uint32_t(Inline),
};

constexpr pure bool TypeIs(Type t, TypeSet set) {return (_mask(t) & uint32_t(set)) != 0;}

constexpr pure bool IsContainer(Type t)         {return TypeIs(t, TypeSet::Container);}

/*  VAL / VALUE DATA FORMAT:
    The low 3 bits are tags giving the type as type Type.
    (The type Null is also used for the values nullish, false and true.)

     Direct/inline types:
         …0000000000000'000 = null/undefined
         …0000000000001'000 = nullish
         …0000000000010'000 = false
         …0000000000011'000 = true
         …iiiiiiiiiiiii'001 = int

     Reference types:
         …ppppppppppppp'010 = float64
         …ppppppppppppp'011 = string
         …ppppppppppppp'100 = symbol
         …ppppppppppppp'101 = blob
         …ppppppppppppp'110 = array
         …ppppppppppppp'111 = dict
 */

// template class with common functionality between Val and Value. Don't use directly.
template <typename RAWVAL>
class ValBase {
public:
    constexpr ValBase()                         :_val(NullVal) { }
    constexpr explicit ValBase(nullptr_t)       :ValBase() { }
    constexpr explicit ValBase(int i)           :_val(encodeInt(i)) { }

    Type type() const pure {
        Type t = rawType();
        if (t == Type::Null) {
            switch(_val) {
                case NullVal:
                case NullishVal:    return Type::Null;
                case FalseVal:
                case TrueVal:       return Type::Bool;
                default:            assert(false);
            }
        }
        return t;
    }

    constexpr bool isNull() const               {return _val == NullVal;}
    constexpr bool isNullish() const            {return _val == NullishVal;}
    constexpr bool isBool() const               {return _val == FalseVal || _val == TrueVal;}
    constexpr bool asBool() const               {return _val > FalseVal;}
    constexpr bool isInt() const                {return rawType() == Type::Int;}
    constexpr int asInt() const                 {assert(isInt()); return int32_t(_val) >> TagSize;}
    constexpr bool isObject() const             {return rawType() > Type::Int;}

    /// True if the value has a numeric type (Int, BigInt or Float.)
    bool isNumber() const pure                          {return TypeIs(type(), TypeSet::Numeric);}

    bool isContainer() const pure                       {return IsContainer(type());}

    template <ValueClass T> bool is() const pure                {return T::HasType(type());}

    /// A Val/Value is "truthy" if it is not `null`. (Yes, `nullish` is truthy.)
    constexpr explicit operator bool() const pure {return !isNull();}

protected:
    friend class Value;
    friend class GarbageCollector;

    enum TagBits : uint32_t {
        TagMask  = 0b111,
    };

    Type rawType() const pure                   {return Type(_val & TagMask);}

    static constexpr int TagSize = 3;

    static constexpr RAWVAL encode(Type t, RAWVAL i) pure  {return (uint32_t(i) << TagSize) | uint32_t(t);}
    static constexpr RAWVAL encodeInt(int i) pure       {return encode(Type::Int, RAWVAL(i));}

    RAWVAL decode() const pure                      {return _val >> TagSize;}

    enum magic : RAWVAL {
        NullVal    = 0b00'000,
        NullishVal = 0b01'000,
        FalseVal   = 0b10'000,
        TrueVal    = 0b11'000,
    };

    constexpr explicit ValBase(magic m)         :_val(m) { }

    RAWVAL _val;
};



/// A 32-bit polymorphic data value inside a Heap.
/// Can be null, boolean, an integer, or a reference to an object in the heap.
/// Application code never creates values of this type; use `Value` instead.
class Val : public ValBase<uintpos> {
public:
    static constexpr int MaxInt = (1 << (sizeof(uintpos)*8 - TagSize - 1)) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val() = default;

    Val& operator= (Val const&);
    Val& operator= (Value);

    /// Returns the value as a number. Unlike `asInt` this supports types other than Int;
    /// it supports Bool, Int, BigInt and Float, and otherwise returns 0.
    template <Numeric NUM> NUM asNumber() const pure;

    /// Returns the value as an Object. Same as `Object(val)`.
    /// Only legal if `isObject()` returns true.
    Object asObject() const pure;

    template <ValueClass T> T as() const pure;
    template <ValueClass T> Maybe<T> maybeAs() const pure;

    friend constexpr bool operator== (Val const& a, Val const& b) pure    {return a._val == b._val;}
    friend constexpr bool operator==(Val const& v, int i) pure      {return v._val == encodeInt(i);}

    friend void swap(Val const&, Val const&);

    //---- Blocks:

   // Val& operator= (Block const* dst);
    void set(Block const*, Type);

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
