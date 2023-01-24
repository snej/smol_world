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
    Int,
};

const char* TypeName(Type t);


template <typename T>
    concept ObjectType = std::is_base_of<Object, T>::value;



/// A 32-bit polymorphic data value associated with a Heap.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class Val {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val()                                     :_val(NullVal) { }
    Val(nullptr_t)                                      :Val() { }

//    constexpr explicit Val(bool b)                      :_val(b ? TrueVal : FalseVal) { }

    constexpr Val(int i)
    :_val(uint32_t((i << TagSize) | IntTag))            {assert(i >= MinInt && i <= MaxInt);}

    constexpr Val(heappos pos)
    :_val(uint32_t(pos) << TagSize)                     {assert(pos != nullpos);}

    Val(Object const&, IN_HEAP);
    Val(Block const* b, IN_HEAP)                        :Val(heap->pos(b)) { }

    Type type(IN_HEAP) const;

    constexpr bool isNull() const                       {return _val == NullVal;}

//    constexpr bool isBool() const                       {return _val == FalseVal || _val == TrueVal;}
//    constexpr int asBool() const                        {return _val > FalseVal;}

    constexpr bool isInt() const                        {return (_val & IntTag) != 0;}
    constexpr int asInt() const                         {assert(isInt()); return int32_t(_val) >> TagSize;}

    constexpr bool isObject() const {
        return (_val & IntTag) == 0 && _val != NullVal;
    }

    Object asObject(IN_HEAP) const;

    Block* asBlock(IN_HEAP) const {
        return isObject() ? (Block*)heap->at(asPos()) : nullptr;
    }

    template <ObjectType T> bool is(IN_HEAP) const      {return type(heap) == T::InstanceType;}

    template <ObjectType T> T as(IN_HEAP) const;

    heappos asPos() const {
        assert(isObject());
        return heappos(_val >> TagSize);
    }

    uint32_t rawBits() const                            {return _val;}

    friend bool operator== (Val a, Val b)               {return a._val == b._val;}
    friend bool operator!= (Val a, Val b)               {return a._val != b._val;}

    // key comparator for Dicts
    static bool keyCmp(Val a, Val b)                    {return a._val > b._val;} // descending order

private:
    enum TagBits : uint32_t {
        IntTag      = 0b001,
    };

    static constexpr int      TagSize = 1;
    static constexpr uint32_t NullVal = 0;

    uintpos _val;
};

static constexpr Val nullval;


std::ostream& operator<<(std::ostream&, Val);


//template <class T> T* Heap:: root() const {
//    return rootVal().as<T>(this);
//}
