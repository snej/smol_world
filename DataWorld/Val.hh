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
    Val(Ptr<T> const& ptr)                              :Val(ptr, T::kTag) { }

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
    Ptr<String> asString() const                        {return asPtr<String>();}

    constexpr bool isArray() const                      {return tag() == ArrayTag;}
    Ptr<Array> asArray() const                          {return asPtr<Array>();}

    constexpr bool isDict() const                       {return tag() == DictTag;}
    Ptr<Dict> asDict() const                            {return asPtr<Dict>();}

    constexpr bool isPtr() const                        {return !isInt() && !isNull();}
    PtrBase asPtrBase() const                           {assert(isPtr()); return PtrBase(asPos());}

    heappos asPos() const {
        assert(isPtr());
        return heappos((_val >> 1) & ~Heap::AlignmentMask);
    }

    template <class T> T* as(Heap const* heap);

    template <class T> Ptr<T> asPtr() const;  // implementation in Objects.hh

private:
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

    static constexpr int TagSize = 3;
    static constexpr uint32_t TagMask = (1 << TagSize) - 1;

    Val(PtrBase const& ptr, TagBits tag)
    :_val((ptr.pos() << 1) | tag)
    { }

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
    ptr = dstVal.asPtr<T>();
}
