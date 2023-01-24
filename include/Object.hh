//
// Object.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "slice.hh"
#include "Block.hh"
#include "Val.hh"


/// A reference to a heap object.
class Object {
public:
    Object()                                    { }
    Object(nullptr_t)                           { }

    Object(Block const* block)                  {if (block) _bytes = block->data<byte>();}

    Object(Val val, IN_HEAP)                    :Object(val.asBlock(heap)) { }

    explicit operator bool() const              {return _bytes.begin() != nullptr;}

    Block* block() const                        {return Block::fromData(_bytes);}
    Val asVal(IN_HEAP) const                    {return Val(block(), heap);}

    Type type() const                           {return block()->type();}

    slice<byte> rawBytes() const                {return _bytes;}

    template <ObjectType T> bool is() const     {return type() == T::InstanceType;}
    template <ObjectType T> T as() const        {return (type() == T::InstanceType) ? *(T*)this : T();}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this object cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    friend bool operator==(Object a, Object b)  {return a._bytes.begin() == b._bytes.begin();}
    friend bool operator!=(Object a, Object b)  {return !(a == b);}

protected:
    friend class GarbageCollector;

    template <typename T> slice<T> dataAs() const    {return slice_cast<T>(_bytes);}

    void relocate(Block* newBlock) {
        _bytes.moveTo((byte*)newBlock->dataPtr());
    }

    slice<byte>  _bytes;
};


std::ostream& operator<< (std::ostream&, Object const&);


/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObject : public Object {
public:
    static constexpr Type InstanceType = TYPE;

protected:
    TypedObject()  :Object() { }
    TypedObject(Block const* block)          :Object(block) { }
    TypedObject(Val val, IN_HEAP)            :Object(val, heap) {assert(type() == TYPE);}

    /// Allocates a new Block with sufficient capacity and constructs a ref on its data.
    TypedObject(size_t capacity, Type type, IN_MUT_HEAP)
    :Object(Block::alloc(capacity, type, heap)) { }
};
