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

enum class ValType : uint8_t {
    Null,
    Bool,
    Int,
    String,
    Array,
    Dict,
};

class Block;
class Object;
class Value;


/** Value types. */
enum class Type : uint8_t {
    // Object types:  (these come from Block type tags, 0..7)
    BigNumber,
    String,
    Symbol,
    Blob,
    Array,
    Dict,
    _spare1,
    _spare2,
    // Primitives:  (these are stored inline in a Val without any pointers)
    Null,
    Bool,
    Int,
};

const char* TypeName(Type t);
std::ostream& operator<<(std::ostream& out, Type);


// template class with common functionality between Val and Value. Don't use directly.
template <typename RAWVAL>
class ValBase {
public:
    constexpr ValBase()                         :_val(NullVal) { }
    constexpr explicit ValBase(nullptr_t)       :ValBase() { }
    constexpr explicit ValBase(bool b)          :_val(b ? TrueVal : FalseVal) { }
    constexpr explicit ValBase(int i)           :_val((i << TagSize) | IntTag) { }

    constexpr bool isNull() const               {return _val == NullVal;}
    constexpr bool isNullish() const            {return _val == NullishVal;}
    constexpr bool isBool() const               {return _val == FalseVal || _val == TrueVal;}
    constexpr bool asBool() const               {return _val > FalseVal;}
    constexpr bool isInt() const                {return (_val & IntTag) != 0;}
    constexpr int asInt() const                 {assert(isInt()); return int32_t(_val) >> TagSize;}
    constexpr bool isObject() const             {return (_val & IntTag) == 0 && _val > TrueVal;}

    constexpr RAWVAL rawBits() const            {return _val;}

    explicit operator bool() const pure         {return !isNull();}

protected:    
    enum TagBits : uint32_t {
        IntTag      = 0b001,
    };

    static constexpr int TagSize = 1;

    enum magic : uint32_t {
        NullVal    = 0,
        NullishVal = 2,
        FalseVal   = 4,
        TrueVal    = 6,
    };

    constexpr explicit ValBase(magic m)         :_val(m) { }

    Type _type() const pure {
        if (isInt())
            return Type::Int;
        else if (isNull() || isNullish())
            return Type::Null;
        else if (isBool())
            return Type::Bool;
        else {
            assert(!isObject());    // fail! Type cannot be resolved w/o dereferencing ptr
            abort();
        }
    }

    RAWVAL _val;
};



enum class relpos : int32_t { Null = 0 };


// base class of Val that lacks the prohibition on copying. Don't use directly.
class ValBase32 : public ValBase<uintpos> {
public:
    static constexpr int MaxInt = (1 << 30) - 1;    ///< Maximum int value, 1,073,741,823
    static constexpr int MinInt = -MaxInt - 1;      ///< Minimum int value, -1,073,741,824

    constexpr ValBase32() = default;
    constexpr explicit ValBase32(nullptr_t)       :ValBase(nullptr) { }
    constexpr explicit ValBase32(bool b)          :ValBase(b) { }
    constexpr explicit ValBase32(int i)           :ValBase(i) {assert(i >= MinInt && i <= MaxInt);}

    ValBase32& operator= (Block const* dst) {
        if (dst) {
            // Convert real pointer to offset:
            intptr_t off = intptr_t(dst) - intptr_t(this);
            assert(off <= INT32_MAX && off >= INT32_MIN); // receiver must be within 2GB of dst!
            _val = uintpos(off) << TagSize;
            assert(isObject()); // offsets of 0,1,2 conflict with values null,false,true
        } else {
            _val = NullVal;
        }
        return *this;
    }

    Block* block() const pure {
        return isObject() ? _block() : nullptr;
    }

    Block* _block() const pure {
        // Convert offset back to real pointer:
        assert(isObject());
        return (Block*)(intptr_t(this) + (int32_t(_val) >> TagSize));
    }

protected:
    constexpr explicit ValBase32(magic m)           :ValBase(m) { }
};



/// A 32-bit polymorphic data value associated with a Heap.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class Val : public ValBase32 {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val() = default;
    constexpr explicit Val(bool b)                      :ValBase32(b) { }
    constexpr explicit Val(int i)                       :ValBase32(i) { }
    explicit Val(Block const* b)                        {*this = b;}
    explicit Val(Object const&);

    Val& operator= (Val const&);
    Val& operator= (Value);
    Val& operator= (Block const* b)                     {ValBase32::operator=(b); return *this;}

    Type type() const pure;

    Object asObject() const pure;

    template <ValueClass T> bool is() const pure                 {return T::HasType(type());}

    template <ValueClass T> T as() const pure;
    template <ValueClass T> Maybe<T> maybeAs() const pure;

    friend bool operator== (Val const& a, Val const& b) pure     {return a._val == b._val;}

    friend std::ostream& operator<<(std::ostream&, Val const&);

    friend void swap(Val const&, Val const&);

    // Yes, the copy constructor is illegal! We cannot allow Val objects to be created on the
    // stack, since the stack may well be > 2GB away from the target address. That means Val cannot
    // be passed by value or returned from a function or used as a local variable.
    Val(Val const&) = delete;

    static constexpr Val _nullish()                             {return Val(ValBase32::NullishVal);}

private:
    constexpr explicit Val(magic m)                             :ValBase32(m) { }
};

static constexpr Val nullval;

/// A Val whose type is Null but isn't the same as nullval. Used for JSON `null`.
static constexpr Val nullishval = Val::_nullish();

}
