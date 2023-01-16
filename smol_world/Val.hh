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

class String;
class Array;
class Dict;


/// A 32-bit polymorphic data value associated with a Heap.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class Val {
public:
    static constexpr int MaxInt = (1 << 28) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val()                                     :_val(NullVal) { }
    Val(nullptr_t)                                      :Val() { }

    constexpr explicit Val(bool b)                      :_val(b ? TrueVal : FalseVal) { }

    constexpr Val(int i)
    :_val(uint32_t((i << 3) | IntTag))                  {assert(i >= MinInt && i <= MaxInt);}

    template <class T>
    Val(T const* ptr, IN_HEAP)                          :Val(heap->pos(ptr), T::Tag) { }

    constexpr ValType type()                            {return kTagType[_val & 0x0F];}

    constexpr bool isNull() const                       {return _val == NullVal;}

    constexpr bool isBool() const                       {return _val == FalseVal || _val == TrueVal;}
    constexpr int asBool() const                        {return _val > FalseVal;}

    constexpr bool isInt() const                        {return tag() == IntTag;}
    constexpr int asInt() const                         {assert(isInt()); return int32_t(_val) >> TagSize;}

    constexpr bool isString() const                     {return tag() == StringTag;}
    String* asString(IN_HEAP) const                     {return as<String>(heap);}

    constexpr bool isArray() const                      {return tag() == ArrayTag;}
    Array* asArray(IN_HEAP) const                       {return as<Array>(heap);}

    constexpr bool isDict() const                       {return tag() == DictTag;}
    Dict* asDict(IN_HEAP) const                         {return as<Dict>(heap);}

    constexpr bool isObject() const                     {return tag() >= StringTag;}
    Object* asObject(IN_HEAP) const                     {return (Object*)heap->at(asPos());}

    heappos asPos() const {
        assert(isObject());
        return heappos((_val >> 1) & ~Heap::AlignmentMask);
    }

    template <class T> T* as(IN_HEAP) const;

    friend bool operator== (Val a, Val b)               {return a._val == b._val;}
    friend bool operator!= (Val a, Val b)               {return a._val != b._val;}

    // key comparator for Dicts
    static bool keyCmp(Val a, Val b)                    {return a._val > b._val;} // descending order

    enum TagBits : uint32_t {
        SpecialTag  = 0b000,       // Special constants like null, false, true
        IntTag      = 0b001,
        StringTag   = 0b010,
        ArrayTag    = 0b011,
        DictTag     = 0b100,

        NullVal   = 0b00000,
        FalseVal  = 0b01000,
        TrueVal   = 0b11000,

        ObjectMask  = 0b00100,
    };

    static constexpr int      TagSize = 3;
    static constexpr uint32_t TagMask = (1 << TagSize) - 1;

    constexpr TagBits tag() const                       {return TagBits(_val & TagMask);}

private:
    friend class GarbageCollector;

    static const std::array<ValType,16> kTagType;

    Val(heappos pos, TagBits tag)
    :_val((uint32_t(pos) << 1) | tag)
    {
        assert(pos > 0 && Heap::isAligned(pos));
    }

    uint32_t _val;
};

static constexpr Val nullval;


std::ostream& operator<<(std::ostream&, Val);


template <class T> T* Heap:: getRootAs() const {
    return root().as<T>(this);
}

//template <class T>
//void GarbageCollector::update(Ptr<T>& ptr) {
//    Val dstVal = scanValue(ptr);
//    ptr = dstVal.as<T>();
//}
