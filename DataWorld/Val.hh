//
// Val.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Heap.hh"
#include <iosfwd>


enum class ValType : uint8_t {
    String,
    Array,
    Dict,
    _Spare,
    Null,
    Int,
};

class String;
class Array;
class Dict;


/// A polymorphic data value.
/// Can be null, an integer, or a reference to a String, Array, or Dict object in the heap.
class Val {
public:
    static constexpr int MaxInt = (1 << 30) - 1;
    static constexpr int MinInt = -MaxInt - 1;

    constexpr Val()                                     :_val(0) { }
    Val(nullptr_t)                                      :Val() { }

    constexpr Val(int i)
    :_val(uint32_t((i << 1) | IntTag))                  {assert(i >= MinInt && i <= MaxInt);}

    template <class T>
    Val(T const* ptr, IN_HEAP)                          :Val(heap->pos(ptr), T::kTag) { }

    constexpr ValType type() {
        if (_val & IntTag)
            return ValType::Int;
        else if (_val == 0)
            return ValType::Null;
        else
            return ValType(tag() >> 1);
    }

    constexpr bool isNull() const                       {return _val == 0;}

    constexpr bool isInt() const                        {return _val & 1;}
    constexpr int asInt() const                         {assert(isInt()); return int32_t(_val) >> 1;}

    constexpr bool isString() const                     {return tag() == StringTag;}
    String* asString(IN_HEAP) const                     {return as<String>(heap);}

    constexpr bool isArray() const                      {return tag() == ArrayTag;}
    Array* asArray(IN_HEAP) const                       {return as<Array>(heap);}

    constexpr bool isDict() const                       {return tag() == DictTag;}
    Dict* asDict(IN_HEAP) const                         {return as<Dict>(heap);}

    constexpr bool isObject() const                     {return !isInt() && !isNull();}

    heappos asPos() const {
        assert(isObject());
        return heappos((_val >> 1) & ~Heap::AlignmentMask);
    }

    template <class T> T* as(IN_HEAP) const;

    friend bool operator== (Val a, Val b)               {return a._val == b._val;}
    friend bool operator!= (Val a, Val b)               {return a._val != b._val;}

    // key comparator for Dicts
    static bool keyCmp(Val a, Val b)                    {return a._val > b._val;} // descending order

private:
    friend class GarbageCollector;
    friend class Heap;
    friend class String;
    friend class Array;
    friend class Dict;

    enum TagBits : uint32_t {
        IntTag      = 0b001,     // Anything with the LSB set is an integer
        StringTag   = 0b000,
        ArrayTag    = 0b010,
        DictTag     = 0b100,
        _spareTag   = 0b110,
    };

    template <Val::TagBits TAG> friend class TaggedObject;
    template <class T, typename ITEM, Val::TagBits TAG> friend class Collection;

    static constexpr int TagSize = 3;
    static constexpr uint32_t TagMask = (1 << TagSize) - 1;

    Val(heappos pos, TagBits tag)
    :_val((pos << 1) | tag)
    {
        assert(Heap::isAligned(pos));
    }

    constexpr TagBits tag() const                       {return TagBits(_val & TagMask);}

    uint32_t _val;
};

static constexpr Val nullval;


std::ostream& operator<<(std::ostream&, Val);


template <class T> T* Heap:: getRootAs() const {
    return root().as<T>(this);
}

template <class T>
void GarbageCollector::update(Ptr<T>& ptr) {
    Val dstVal = scanValue(ptr);
    ptr = dstVal.as<T>();
}
