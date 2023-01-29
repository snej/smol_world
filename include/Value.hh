//
// Value.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Block.hh"
#include "slice.hh"
#include "Val.hh"


template <class T> class Maybe;


/// Value is like Val, but with a full pointer to the object (if it's an object.)
/// Used in memory, never stored in the heap.
class Value {
public:
    constexpr Value()                                   :_val() { }
    constexpr Value(nullptr_t)                          :Value() { }
    constexpr explicit Value(bool b)                    :_val(b) { }
    constexpr Value(int i)                              :_val(i) { }

    explicit Value(Val const& val) {
        if (Block *block = val.block())
            setBlock(block);
        else
            _val = val;
    }

    explicit Value(Block const* block) {
        if (block)
            setBlock(block);
        else
            _val = nullval;
    }

    Type type() const                               {return _ptr ? block()->type() : _val._type();}
    explicit operator bool() const                  {return !isNull();}
    constexpr bool isNull() const                   {return _val.isNull();}

    constexpr bool isBool() const                   {return _val.isBool();}
    constexpr bool asBool() const                   {return _val.asBool();}

    constexpr bool isInt() const                    {return _val.isInt();}
    constexpr int asInt() const                     {return _val.asInt();}

    bool isObject() const                           {return _ptr != nullptr;}
    Object const& asObject() const                  {assert(isObject()); return *(Object*)this;}

    template <ValueClass T> bool is() const         {return T::HasType(type());}
    template <ValueClass T> T as() const            {assert(is<T>()); return *(T*)this;}
    template <ValueClass T> Maybe<T> maybeAs() const {return Maybe<T>(*this);}

    /// Calls `fn`, which must be a generic lambda taking an `auto` parameter,
    /// with this value cast to its runtime type.
    template <typename FN> bool visit(FN fn) const;

    friend bool operator==(Value const& a, Value const& b)  {return a._val == b._val && a._ptr == b._ptr;}
    friend bool operator!=(Value const& a, Value const& b)  {return !(a == b);}
    friend bool operator==(Val const& val, Value const& value);

    ValBase asValBase() const                       {assert(!_ptr); return _val;}
    Block* block() const                        {assert(_ptr); return Block::fromData(rawBytes());}

protected:
    friend class GarbageCollector;
    friend class Heap;
    friend class Val;

    slice<byte> rawBytes() const                {assert(_ptr); return {(byte*)_ptr, _size};}

private:
    static constexpr ValBase PlaceholderPosVal = ValBase(relpos(-1));

    void setBlock(Block const* block) {
        auto bytes = block->data<byte>();
        _ptr = bytes.begin();
        _size = bytes.size();
        _val = PlaceholderPosVal;
    }

    void relocate(Block* newBlock) {
        assert(_ptr);
        _ptr = newBlock->dataPtr();
    }

    ValBase  _val;              // The equivalent Val, but the pointer bits aren't valid
    uint32_t _size = 0;         // Data size
    void*    _ptr = nullptr;    // Data address
};

constexpr Value nullvalue;

std::ostream& operator<< (std::ostream&, Value const&);



/// A `std::optional`-like type for Value classes.
template <class T>
class Maybe {
public:
    Maybe() = default;
    Maybe(nullptr_t)                    :Maybe() { }
    explicit Maybe(Value const& val)    {if (T::HasType(val.type())) _val = val;}
    Maybe(T const& obj) :_val(obj)      { }

    explicit operator bool() const      {return !_val.isNull();}
    operator Value() const              {return _val;}

    T& value()                          {return *getp();}
    T const& value() const              {return const_cast<Maybe*>(this)->get();}

    // do not call this directly! It's only for use by MAYBE()
    friend T _unsafeval_(Maybe<T> const& m)     {return reinterpret_cast<T const&>(m._val);}

    friend std::ostream& operator<< (std::ostream& o, Maybe const& m) {return o << m._val;}

private:
    T* getp()                           {assert(_val); return reinterpret_cast<T*>(&_val);}

    Value _val;
};


#define if_let(VAR, EXPR)  if (auto VAR = _unsafeval_(EXPR); !VAR.isNull())
#define unless(VAR, EXPR)  auto VAR = _unsafeval_(EXPR); if (VAR.isNull())



/// A reference to a heap object -- any type except Int and Null.
class Object : public Value {
public:
    static bool HasType(enum Type t) {return t < Type::Null;}

    explicit Object(Block const* block)             :Value(block) {assert(isObject());}
    explicit Object(Val const& val)                 :Value(val) {assert(isObject());}

    Block* block() const                            {return Value::block();}
    slice<byte> rawBytes() const                    {return Value::rawBytes();}

    explicit operator bool() const = delete;

protected:
    template <typename T> slice<T> dataAs() const   {return slice_cast<T>(rawBytes());}
};


/// The Value subclass representing the Null type.
class Null : public Value {
public:
    static constexpr Type Type = Type::Null;
    static bool HasType(enum Type t) {return t == Type;}

    Null() = default;
    Null(nullptr_t)                     :Value() { }
};


/// The Value subclass representing the Bool type.
class Bool : public Value {
public:
    static constexpr Type Type = Type::Bool;
    static bool HasType(enum Type t) {return t == Type;}

    explicit Bool(bool b = false)       :Value(b) { }
    explicit operator bool() const      {return asBool();}
};


/// The Value subclass representing the Int type.
class Int : public Value {
public:
    static constexpr Type Type = Type::Int;
    static bool HasType(enum Type t) {return t == Type;}

    Int(int i = 0)          :Value(i) { }
    operator int() const    {return asInt();}

    friend bool operator==(Int const& a, int b)  {return a.asInt() == b;}
    friend bool operator==(int a, Int const& b)  {return a == b.asInt();}
};


/// An Object subclass that implements a particular Type code.
template <Type TYPE>
class TypedObject : public Object {
public:
    static constexpr Type Type = TYPE;
    static bool HasType(enum Type t) {return t == Type;}
protected:
    explicit TypedObject(Block const* block)        :Object(block) { }
    explicit TypedObject(Val const& val)            :Object(val) {assert(type() == TYPE);}
};
