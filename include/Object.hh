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


/// Abstract base of classes that live in a Heap and are referred to with Vals.
class Object {
public:

    Type type() const                           {assert(!isForwarded()); return Type(tags() >> 1);}

    Val asVal(IN_HEAP) const                    {return Val(this, heap);}

    template <class T> bool is() const          {return type() == T::InstanceType;}
    template <class T> T* as()                  {return (type() == T::InstanceType) ? (T*)this : nullptr;}
    template <class T> T const* as() const      {return const_cast<Object*>(this)->as<T>();}

private:
    // Tag bits stored in an Object's meta word, alongsize its size.
    enum Tags : uint8_t {
        Fwd          = 0b00001, // If set, all 31 remaining bits are the forwarding address

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

        TagsMask     = 0b01111, // all tags
        TypeMask     = 0b01110, // type tags

        Visited      = 0b10000,
    };

    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << 1);}

    static constexpr heapsize TagBits = 5;

protected:
    static constexpr heapsize MaxSize = 1 << (32 - TagBits);

    static void* operator new(size_t size, IN_MUT_HEAP, size_t extraSize) {
        assert(size < MaxSize);
        return heap->alloc(heapsize(size + extraSize));
    }

    explicit Object(heapsize totalSize, Type type)
    :_meta((heapsize(totalSize - sizeof(Object)) << TagBits) | typeTag(type)) { }

    Tags tags() const                           {return Tags(_meta & TagsMask);}

    /// The integer stored in the object header (minus the tags.)
    /// Collections use this as an item count; other classes can use it for other purposes.
    heapsize dataSize() const                   {assert(!isForwarded()); return _meta >> TagBits;}

    /// A pointer to just after the object header (the `_meta` field.) Used by collections.
    void* data()                                {return &_meta + 1;}
    const void* data() const                    {return &_meta + 1;}

private:
    friend class Heap;
    friend class GarbageCollector;

    // Stuff used by GC:
    static constexpr bool typeContainsPointers(Type type) {
        return type >= Type::Array && type <= Type::Dict;
    }
    bool isVisited() const                      {return (_meta & Visited) != 0;}
    void setVisited()                           {_meta |= Visited;}
    void clearVisited()                         {_meta &= ~Visited;}
    bool isForwarded() const                    {return (_meta & Fwd) != 0;}
    heappos getForwardingAddress() const        {return heappos(isForwarded() ? (_meta >> 1) : 0);}
    void setForwardingAddress(heappos addr) {
        assert(addr > 0 && !(uintpos(addr) & Fwd));
        _meta = (uintpos(addr) << 1) | Fwd;
    }

    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    heapsize _meta;
};


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
