//
// Val.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Heap.hh"
#include <array>
#include <iosfwd>


enum class ValType : uint8_t {
    Null,
    Bool,
    Int,
    String,
    Array,
    Dict,
};

class Block;
class String;
class Array;
class Dict;
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



/// A 32-bit polymorphic data value associated with a Heap.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class ValBase {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr ValBase()                                 :_val(NullVal) { }
    constexpr explicit ValBase(nullptr_t)               :ValBase() { }
    constexpr explicit ValBase(bool b)                  :_val(b ? TrueVal : FalseVal) { }
    constexpr explicit ValBase(int i)                   :_val(uint32_t((i << TagSize) | IntTag))
                                                        {assert(i >= MinInt && i <= MaxInt);}
    constexpr explicit ValBase(heappos pos)             :_val(uint32_t(pos) << TagSize)
                                                        {assert(pos != nullpos);}
    constexpr bool isNull() const                       {return _val == NullVal;}

    constexpr bool isBool() const                       {return _val == FalseVal || _val == TrueVal;}
    constexpr bool asBool() const                       {return _val > FalseVal;}

    constexpr bool isInt() const                        {return (_val & IntTag) != 0;}
    constexpr int asInt() const                         {assert(isInt()); return int32_t(_val) >> TagSize;}

    constexpr bool isObject() const {
        return (_val & IntTag) == 0 && _val > TrueVal;
    }

    heappos asPos() const {
        assert(isObject());
        return heappos(_val >> TagSize);
    }

    uint32_t rawBits() const                                {return _val;}

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
    constexpr explicit Val(heappos p)                   :ValBase(p) { }
    Val(Object const&, IN_HEAP);
    Val(Block const* b, IN_HEAP)                        :Val(heap->pos(b)) { }

    Val& operator= (Value);

    Type type(IN_HEAP) const;

    Object asObject(IN_HEAP) const;

    Block* asBlock(IN_HEAP) const {
        return isObject() ? (Block*)heap->at(asPos()) : nullptr;
    }

    template <ValueClass T> bool is(IN_HEAP) const      {return T::HasType(type(heap));}

    template <ValueClass T> T as(IN_HEAP) const;
    template <ValueClass T> Maybe<T> maybeAs(IN_HEAP) const;

    friend bool operator== (Val const& a, Val const& b)     {return a._val == b._val;}
    friend bool operator!= (Val const& a, Val const& b)     {return a._val != b._val;}
};

static constexpr Val nullval;


std::ostream& operator<<(std::ostream&, Val const&);


//template <class T> T* Heap:: root() const {
//    return rootVal().as<T>(this);
//}
