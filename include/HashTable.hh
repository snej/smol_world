//
// HashTable.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include "Heap.hh"
#include "SparseArray.hh"
#include "function_ref.hh"

namespace snej::smol {

class HashSet {
public:
    static int32_t computeHash(std::string_view) pure;
    static int32_t computeHash(Value) pure;

    static Array createArray(Heap &heap, uint32_t capacity);

    HashSet(Heap &heap, Array array);
    HashSet(Heap &heap, unsigned capacity);

    Array array() const pure                        {return _array.array();}
    Heap& heap() const pure                         {return *_heap;}
    void setHeap(Heap &heap)                        {_heap = &heap; _array.setHeap(heap);}

//    Array array() const pure                        {return _array;}
    uint32_t count() const pure                     {return _count;}
    uint32_t capacity() const pure                  {return _capacity;}

    /// Returns the existing key equal to this string, if any.
    Value find(std::string_view str) const;

    /// Returns the existing key equal to this string; else calls the `creator` function,
    /// which should return `Value`, and adds the resulting value.
    /// Returns null if the creator function returned null, or if growing the table failed.
    template <typename FN>
    Value findOrInsert(std::string_view str, FN creator) {
        int32_t hashCode = computeHash(str);
        if (auto [i, found] = search(str, hashCode); found) {
            return _array[i];
        } else {
            Handle symbol(creator(*_heap), *_heap);
            if (!insert(symbol, hashCode, i))
                symbol = nullvalue;
            return symbol;
        }
    }

    /// Inserts a key, but only if there's no existing one.
    /// Returns false if it's a duplicate, or if growing the table failed.
    [[nodiscard]] bool insert(Value key)          {return insert(key, computeHash(key));}

    using Visitor = function_ref<bool(Value key)>;

    /// Calls the `visitor` callback once with each Value (and its hash code.)
    bool visit(Visitor visitor) const;

    void dump(std::ostream&, bool longForm = false) const;

protected:
    static bool keysMatch(Value key1, Value key2) pure;
    static bool keysMatch(std::string_view key1, Value key2) pure;
    static SparseArray _createArray(Heap &heap, uint32_t capacity);

    HashSet(Heap &heap, SparseArray&&, bool recount);
    template <typename KEY>
        std::pair<unsigned,bool> search(KEY key, int32_t hashCode) const pure;
    [[nodiscard]] bool insert(Value key, int32_t hashCode);
    [[nodiscard]] bool insert(Value key, int32_t hashCode, unsigned i);
    [[nodiscard]] bool grow();

    Heap*           _heap;
    SparseArray     _array;
    uint32_t        _size;
    uint32_t        _count;
    uint32_t        _capacity;
};

}
