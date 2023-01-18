//
// Object.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Val.hh"
#include <initializer_list>
#include <string_view>


using string_view = std::string_view;


template <typename T>
struct slice {
    T*       data;
    uint32_t count;

    using iterator = T*;
    T* begin() const    {return data;}
    T* end() const      {return data + count;}

    T& operator[] (uint32_t i)  {assert(i < count); return data[i];}
};


/// Abstract base of classes that live in a Heap and are referred to with Vals.
class Object {
public:

    Type type() const                           {assert(!isForwarded());
                                                 return Type((_tags & TypeMask) >> 1);}

    Val asVal(IN_HEAP) const                    {return Val(this, heap);}

    template <class T> bool is() const          {return type() == T::InstanceType;}
    template <class T> T* as()                  {return (type() == T::InstanceType) ? (T*)this : nullptr;}
    template <class T> T const* as() const      {return const_cast<Object*>(this)->as<T>();}

private:
    static constexpr heapsize TagBits = 6;

    // Tag bits stored in an Object's meta word, alongsize its size.
    enum Tags : uint8_t {
        Fwd          = 0b000001, // If set, all 31 remaining bits are the forwarding address

        Large        = 0b010000,    // If set, size is 32-bit not 16-bit
        Visited      = 0b100000,    // Marker used by Heap::visit()

        // scalars:
        TypeBigNumber= uint8_t(Type::BigNumber) << 1,
        TypeString   = uint8_t(Type::String) << 1,
        TypeSymbol   = uint8_t(Type::Symbol) << 1,
        TypeBlob     = uint8_t(Type::Blob) << 1,
        // contain pointers:
        TypeArray    = uint8_t(Type::Array) << 1,
        TypeDict     = uint8_t(Type::Dict) << 1,
        Type_spare1  = uint8_t(Type::_spare1) << 1,
        Type_spare2  = uint8_t(Type::_spare2) << 1,

        TagsMask     = (1 << TagBits) - 1,    // all tags
        TypeMask     = 0b001110,    // type tags
    };

    /// Static function that maps a Type to its Tags value
    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << 1);}

protected:
    static constexpr heapsize MaxSize = 1 << (32 - TagBits);
    static constexpr heapsize LargeSize = 1 << (16 - TagBits);

    static void* operator new(size_t size, IN_MUT_HEAP, size_t extraSize) {
        assert(size < MaxSize);
        return heap->alloc(heapsize(size + extraSize));
    }

    explicit Object(heapsize totalSize, Type type) {
        heapsize dataSize = totalSize - 2;
        uint32_t meta = (dataSize << TagBits) | typeTag(type);
        if (meta > 0xFFFF)
            meta |= Large;
        _meta = meta;
    }

    Tags tags() const                           {return Tags(_tags & TagsMask);}

    uint32_t& bigMeta() const                   {return *(uint32_t*)this;}

    /// The integer stored in the object header (minus the tags.)
    /// Collections use this as an item count; other classes can use it for other purposes.
    heapsize dataSize() const {
        assert(!isForwarded());
        uint32_t meta = bigMeta();
        if (!(meta & Large))
            meta &= 0xFFFF;
        return meta >> TagBits;
    }

    /// A pointer to just after the object header (the `_meta` field.) Used by collections.
    void* dataPtr() {
        return (byte*)this + ((_tags & Large) ? 4 : 2);
    }

    const void* dataPtr() const                 {return const_cast<Object*>(this)->dataPtr();}

    /// Returns both the data pointer and size; slightly more efficient.
    template <typename T = byte>
    slice<T> data() {
        assert(!isForwarded());
        if (uint32_t meta = bigMeta(); meta & Large) 
            return {(T*)((byte*)this + 4), uint32_t((meta >> TagBits) / sizeof(T))};
        else
            return {(T*)((byte*)this + 2), uint32_t(((meta &= 0xFFFF) >> TagBits) / sizeof(T))};
    }

    Object* nextObject()                        {return (Object*)(data().end());}

private:
    friend class Heap;
    friend class GarbageCollector;

    // Stuff used by GC:
    static constexpr bool typeContainsPointers(Type type) {
        return type >= Type::Array && type <= Type::Dict;
    }
    bool isVisited() const                      {return (_tags & Visited) != 0;}
    void setVisited()                           {_tags |= Visited;}
    void clearVisited()                         {_tags &= ~Visited;}
    bool isForwarded() const                    {return (_tags & Fwd) != 0;}
    heappos getForwardingAddress() const        {return heappos(isForwarded() ? (bigMeta() >> 1) : 0);}
    void setForwardingAddress(heappos addr) {
        assert(addr > 0 && !(uintpos(addr) & Fwd));
        bigMeta() = (uintpos(addr) << 1) | Fwd;
    }

    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    union {  // Note: This layout assumes little-endian byte order
        uint8_t  _tags;
        uint16_t _meta;
        //uint32_t _bigMeta;
    };
} __attribute__((aligned (1))) __attribute__((packed));

static_assert(sizeof(Object) == 2);
static_assert(alignof(Object) == 1);


std::ostream& operator<< (std::ostream&, Object const*);


/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObject : public Object {
public:
    static constexpr Type InstanceType = TYPE;

    Val asVal(IN_HEAP) const    {return Val(this, heap);}

protected:
    explicit TypedObject(heapsize totalSize) :Object(totalSize, InstanceType) { }
};


template <class T>
T* Val::as(IN_HEAP) const {
    if (auto obj = asObject(heap); obj && obj->type() == T::InstanceType)
        return (T*)obj;
    return nullptr;
}
