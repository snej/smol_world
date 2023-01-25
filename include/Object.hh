//
// Object.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "slice.hh"
#include "Block.hh"
#include "Val.hh"


/// Value is like Val, but with a full pointer to the object (if it's an object.)
/// Used in memory, never stored in the heap.
class Value {
public:
    constexpr Value()                                   :_val(nullval) { }
    Value(nullptr_t)                                    :Value() { }

    //    constexpr explicit Value(bool b)                      :_val(b ? TrueVal : FalseVal) { }

    constexpr Value(int i)                              :_val(i) { }

    Value(Val val, IN_HEAP)
    :_val(val) {
        if (Block *block = val.asBlock(heap)) {
            auto bytes = block->data<byte>();
            _ptr = bytes.begin();
            _size = bytes.size();
        }
    }

    Value(Block const* block, IN_HEAP) {
        if (block) {
            _val = Val(block, heap);
            auto bytes = block->data<byte>();
            _ptr = bytes.begin();
            _size = bytes.size();
        } else {
            _val = nullval;
        }
    }

    operator Val() const                            {return _val;}
    Type type() const                               {return _ptr ? block()->type() : _val._type();}
    explicit operator bool() const                  {return !isNull();}
    constexpr bool isNull() const                   {return _val == nullval;}

    constexpr bool isInt() const                    {return _val.isInt();}
    constexpr int asInt() const                     {return _val.asInt();}

    bool isObject() const                           {return _ptr != nullptr;}
    Object const& asObject() const                  {assert(isObject()); return *(Object*)this;}

    template <ValueClass T> bool is() const     {return type() == T::InstanceType;}
    template <ValueClass T> T as() const        {return (type() == T::InstanceType) ? *(T*)this : T();}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this object cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    friend bool operator==(Value const& a, Value const& b)  {return a._val == b._val && a._ptr == b._ptr;}
    friend bool operator!=(Value const& a, Value const& b)  {return !(a == b);}

protected:
    Block* block() const                        {assert(_ptr); return Block::fromData(rawBytes());}
    slice<byte> rawBytes() const                {assert(_ptr); return {(byte*)_ptr, _size};}

private:
    friend class GarbageCollector;

    void relocate(Block* newBlock)              {assert(_ptr); _ptr = newBlock->dataPtr();}

    Val      _val;
    uint32_t _size = 0;
    void*    _ptr = nullptr;
};

static_assert(sizeof(Value) == 16);


/// A reference to a heap object -- any type except Int and Null.
class Object : public Value {
public:
    Object() = default;
    Object(nullptr_t)                               { }

    Object(Block const* block, IN_HEAP)             :Value(block, heap) {assert(isObject());}
    Object(Val val, IN_HEAP)                        :Value(val, heap) {assert(isObject());}

    Block* block() const                            {return Value::block();}
    slice<byte> rawBytes() const                    {return Value::rawBytes();}

protected:
    template <typename T> slice<T> dataAs() const   {return slice_cast<T>(rawBytes());}
};


std::ostream& operator<< (std::ostream&, Object const&);


/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObject : public Object {
public:
    static constexpr Type InstanceType = TYPE;

protected:
    TypedObject()  :Object() { }
    TypedObject(Block const* block, IN_HEAP)          :Object(block, heap) { }
    TypedObject(Val val, IN_HEAP)            :Object(val, heap) {assert(type() == TYPE);}

    /// Allocates a new Block with sufficient capacity and constructs a ref on its data.
    TypedObject(size_t capacity, Type type, IN_MUT_HEAP)
    :Object(Block::alloc(capacity, type, heap), heap) { }
};


class NullValue : public Value {
public:
    static constexpr Type InstanceType = Type::Null;

    NullValue() = default;
    NullValue(nullptr_t)    :Value() { }
};


class IntValue : public Value {
public:
    static constexpr Type InstanceType = Type::Int;

    IntValue(int i = 0)     :Value(i) { }
    operator int() const    {return asInt();}
};
