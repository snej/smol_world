//
// Object.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "slice.hh"
#include "Block.hh"
#include "Val.hh"
#include <initializer_list>
#include <string_view>


using string_view = std::string_view;
class String; class Symbol; class Blob; class Array; class Dict;


/// A reference to a heap object.
class ObjectRef {
public:
    Block* block() const                        {return Block::fromData(_bytes);}
    Val asVal(IN_HEAP) const                    {return Val(block(), heap);}

    Type type() const                           {return block()->type();}

    template <class T> bool is() const          {return type() == T::InstanceType;}
    template <class T> T* as()                  {return (type() == T::InstanceType) ? (T*)this : nullptr;}
    template <class T> T const* as() const      {return const_cast<ObjectRef*>(this)->as<T>();}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this object cast to its runtime type.
    template <typename FN>
    bool visit(FN fn) const {
        switch (type()) {
            case Type::String: fn(as<String>()); break;
            case Type::Symbol: fn(as<Symbol>()); break;
            case Type::Blob:   fn(as<Blob>()); break;
            case Type::Array:  fn(as<Array>()); break;
            case Type::Dict:   fn(as<Dict>()); break;
            default:           return false;
        }
        return true;
    }

protected:
    friend class GarbageCollector;

    ObjectRef(Block const* block, IN_HEAP)
    :_bytes(block->data<byte>()) {
        heap->registerExternalRoot(this);
    }
    ObjectRef(Val val, IN_HEAP)
    :ObjectRef(val.asBlock(heap), heap) { }

    template <typename T> slice<T> dataAs() const    {return slice_cast<T>(_bytes);}

    void relocate(Block* newBlock) {
        assert(newBlock->type() == type());
        assert(newBlock->dataSize() == _bytes.size());
        _bytes.moveTo((byte*)newBlock->dataPtr());
    }

    slice<byte>  _bytes;
};


std::ostream& operator<< (std::ostream&, ObjectRef const&);


/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObjectRef : public ObjectRef {
public:
    static constexpr Type InstanceType = TYPE;

protected:
    TypedObjectRef(Block const* block, IN_HEAP)  :ObjectRef(block, heap) { }
    TypedObjectRef(Val val, IN_HEAP)    :ObjectRef(val, heap) {assert(type() == TYPE);}
    TypedObjectRef(size_t capacity, Type type, IN_MUT_HEAP)
    :ObjectRef(Block::alloc(capacity, type, heap), heap) { }
};


/*
template <class T>
T* Val::as(IN_HEAP) const {
    if (auto obj = asObject(heap); obj && obj->type() == T::InstanceType)
        return (T*)obj;
    return nullptr;
}
*/
