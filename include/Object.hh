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
    slice(T* b, T* e)       :_begin(b), _end(e) {assert(e >= b);}
    slice(T* b, uint32_t s) :_begin(b), _end(b + s) { }

    using iterator = T*;
    T* begin() const    {return _begin;}
    T* end() const      {return _end;}

    size_t size() const {return _end - _begin;}
    bool empty() const  {return _end == _begin;}

    T& operator[] (uint32_t i)  {auto ptr = &_begin[i]; assert(ptr < _end); return *ptr;}
private:
    T* _begin;
    T* _end;
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
        Fwd          = 0b000001,    // If set, all 31 remaining bits are the forwarding address

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

        TypeMask     = 0b001110,            // type tags
        TagsMask     = (1 << TagBits) - 1,  // all tags
    };

    /// Static function that maps a Type to its Tags value
    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << 1);}

protected:
    static constexpr heapsize MaxSize = 1 << (32 - TagBits);
    static constexpr heapsize LargeSize = 1 << (16 - TagBits);

    static void* operator new(size_t size, IN_MUT_HEAP, size_t dataSize) {
        assert(size == sizeof(Object)); // subclasses must not add extra data members!
        if (dataSize == 0) {
            size = 4;  // Object must allocate at least enough space to store forwarding pos
        } else {
            size += dataSize;
            if (dataSize >= LargeSize)
                size += 2;      // Add room for 32-bit dataSize
            assert(size < MaxSize);
        }
        return heap->alloc(heapsize(size));
    }

    explicit Object(heapsize dataSize, Type type) {
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

    Object* nextObject() {
        auto dat = data();
        return (Object*)( dat.begin() + std::max(dat.size(), sizeof(uint32_t)) );
    }

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
