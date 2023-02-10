//
// HashTable.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include "Heap.hh"
#include "function_ref.hh"

namespace snej::smol {

class HashTable {
public:
    static int32_t computeHash(std::string_view) pure;
    static int32_t computeHash(Value) pure;

    Heap& heap() const pure                         {return *_heap;}
    void setHeap(Heap &heap)                        {_heap = &heap; _array.setHeap(heap);}

    Array array() const pure                        {return _array;}
    uint32_t count() const pure                     {return _count;}
    uint32_t capacity() const pure                  {return _capacity;}

    using Visitor = function_ref<bool(Value key, Value value)>;

    /// Calls the `visitor` callback once with each Value (and its hash code.)
    bool visit(Visitor visitor) const;

    void dump(std::ostream&, bool includeEmpty =false) const;

protected:
    static bool keysMatch(Value key1, Value key2) pure;
    static bool keysMatch(std::string_view key1, Value key2) pure;
    static Maybe<Array> createArray(Heap &heap, uint32_t capacity, bool withValues);

    HashTable(Heap &heap, Array array, bool hasValues, bool recount);
    slice<Val> hashes() const                       {return {(Val*)&_array[0],     _size};}
    slice<Val> keys() const                         {return {(Val*)&_array[_size], _size};}
    template <typename KEY>
        std::pair<Val*,bool> search(KEY key, int32_t hashCode) const pure;
    bool insert(Value key, int32_t hashCode, Value value);
    bool insert(Value key, int32_t hashCode, Val* entry, Value value);
    bool grow();

    Heap*           _heap;
    Handle<Array>   _array;
    uint32_t        _size;
    uint32_t        _count;
    uint32_t        _capacity;
    bool            _hasValues;
};


class HashMap : public HashTable {
public:
    static Maybe<Array> createArray(Heap &heap, uint32_t capacity) {
        return HashTable::createArray(heap, capacity, true);
    }

    explicit HashMap(Heap &heap, Array array)     :HashMap(heap, array, true) { }

    /// Returns the existing value for this key, if any.
    Value get(Value key) const;

    /// Inserts a value for this key, but only if there's no existing one.
    /// Returns false if it's a duplicate, or if growing the table failed.
    bool insert(Value key, Value value)     {return set(key, value, false);}

    bool put(Value key, Value value)        {return set(key, value, true);}

    /// Returns the existing vlue for this key; else calls the `creator` function,
    /// which should return `Value`, and adds the resulting value.
    /// Returns null if the creator function returned null, or if growing the table failed.
    template <typename FN>
    Value findOrInsert(Value key, FN creator) {
        int32_t hashCode = computeHash(key);
        if (auto [entry, found] = search(key, hashCode); found) {
            return entry[2*_size];
        } else {
            Value value = creator(*_heap);
            if (value && !HashTable::insert(key, hashCode, entry, value))
                value = nullvalue;
            return value;
        }
    }

private:
    HashMap(Heap &heap, Array array, bool recount) :HashTable(heap, array, recount, true) {}
    bool set(Value key, Value value, bool replace);
};




class HashSet : public HashTable {
public:
    static Maybe<Array> createArray(Heap &heap, uint32_t capacity) {
        return HashTable::createArray(heap, capacity, false);
    }

    explicit HashSet(Heap &heap, Array array)     :HashSet(heap, array, true) { }

    /// Returns the existing key equal to this string, if any.
    Value find(std::string_view str) const;

    /// Inserts a key, but only if there's no existing one.
    /// Returns false if it's a duplicate, or if growing the table failed.
    bool insert(Value key)          {return HashTable::insert(key, computeHash(key), nullvalue);}

    /// Returns the existing key equal to this string; else calls the `creator` function,
    /// which should return `Value`, and adds the resulting value.
    /// Returns null if the creator function returned null, or if growing the table failed.
    template <typename FN>
    Value findOrInsert(std::string_view str, FN creator) {
        int32_t hashCode = computeHash(str);
        if (auto [entry, found] = search(str, hashCode); found) {
            return entry[_size];
        } else {
            Value symbol = creator(*_heap);
            if (!HashTable::insert(symbol, hashCode, entry, nullvalue))
                symbol = nullvalue;
            return symbol;
        }
    }

private:
    HashSet(Heap &heap, Array array, bool recount) :HashTable(heap, array, false, recount) {}
};

}
