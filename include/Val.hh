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
    // scalars:
    BigNumber,
    String,
    Symbol,
    Blob,
    // collections of values:
    Array,
    Dict,
    _spare1,
    _spare2,
    // Primitives:
    Null,
    Bool,
    Int,
};

const char* TypeName(Type t);
std::ostream& operator<<(std::ostream& out, Type);


template <typename T>
concept ValueClass = std::is_base_of<Value, T>::value;
template <class T> class Maybe;


enum class relpos : int32_t { Null = 0 };


// base class of Val that lacks the prohibition on copying. For internal use.
class ValBase {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr ValBase()                         :_val(NullVal) { }
    constexpr explicit ValBase(nullptr_t)       :ValBase() { }
    constexpr explicit ValBase(bool b)          :_val(b ? TrueVal : FalseVal) { }
    constexpr explicit ValBase(int i)           :_val(uint32_t((i << TagSize) | IntTag))
                                                    {assert(i >= MinInt && i <= MaxInt);}
    constexpr explicit ValBase(relpos pos)      {setRelPos(pos);}

    constexpr bool isNull() const               {return _val == NullVal;}
    constexpr bool isBool() const               {return _val == FalseVal || _val == TrueVal;}
    constexpr bool asBool() const               {return _val > FalseVal;}
    constexpr bool isInt() const                {return (_val & IntTag) != 0;}
    constexpr int asInt() const                 {assert(isInt()); return int32_t(_val) >> TagSize;}

    constexpr bool isObject() const             {return (_val & IntTag) == 0 && _val > TrueVal;}

    uint32_t rawBits() const                    {return _val;}

    ValBase& operator= (Block const* dst) {
        if (dst) {
            auto off = intptr_t(dst) - intptr_t(this);
            assert(off <= INT32_MAX && off >= INT32_MIN);
            setRelPos(relpos(off));
        } else {
            _val = NullVal;
        }
        return *this;
    }

    Block* block() const {
        return isObject() ? _block() : nullptr;
    }

    Block* _block() const {
        assert(isObject());
        return (Block*)(intptr_t(this) + int32_t(relPos()));
    }

protected:
    friend class Value;
    
    enum TagBits : uint32_t {
        IntTag      = 0b001,
    };

    static constexpr int      TagSize  = 1;

    static constexpr uint32_t NullVal  = 0;
    static constexpr uint32_t FalseVal = 2;
    static constexpr uint32_t TrueVal  = 4;

    constexpr explicit ValBase(uint32_t val)               :_val(val) { }
    Type _type() const;

    relpos relPos() const {
        assert(isObject());
        return relpos(int32_t(_val) >> TagSize);
    }

    constexpr void setRelPos(relpos pos) {
        assert(pos != relpos::Null);
        _val = uint32_t(pos) << TagSize;
    }

    friend bool operator== (ValBase const& a, ValBase const& b)     {return a._val == b._val;}
    friend bool operator!= (ValBase const& a, ValBase const& b)     {return a._val != b._val;}

    uintpos _val;
};



/// A 32-bit polymorphic data value associated with a Heap.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class Val : public ValBase {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val()                                     :ValBase(NullVal) { }
    constexpr explicit Val(bool b)                      :ValBase(b) { }
    constexpr explicit Val(int i)                       :ValBase(i) { }
    constexpr explicit Val(relpos p)                    :ValBase(p) { }
    explicit Val(Block const* b)                        {*this = b;}
    explicit Val(Object const&);

    Val& operator= (Val const&);
    Val& operator= (Value);
    Val& operator= (Block const* b)                     {ValBase::operator=(b); return *this;}

    Type type() const;

    Object asObject() const;

    template <ValueClass T> bool is() const                 {return T::HasType(type());}

    template <ValueClass T> T as() const;
    template <ValueClass T> Maybe<T> maybeAs() const;

    friend bool operator== (Val const& a, Val const& b)     {return a._val == b._val;}
    friend bool operator!= (Val const& a, Val const& b)     {return a._val != b._val;}

    friend std::ostream& operator<<(std::ostream&, Val const&);

    friend void swap(Val const&, Val const&);
    Val(Val const&) = delete;
};

static constexpr Val nullval;

}
