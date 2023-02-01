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
    static int32_t computeHash(std::string_view);

    static std::unique_ptr<HashTable> create(Heap &heap, uint32_t capacity);

    explicit HashTable(Heap &heap, Array array)     :HashTable(heap, array, true) { }

    Heap& heap() const pure                         {return *_heap;}
    void setHeap(Heap &heap)                        {_heap = &heap; _array.setHeap(heap);}

    Array array() const pure                        {return _array;}
    uint32_t count() const pure                     {return _count;}
    uint32_t capacity() const pure                  {return _capacity;}

    /// Returns the existing value for this key, if any.
    Value find(string_view str) const       {return search(str, computeHash(str)).second;}

    /// Inserts the value for this key, but only if there's no existing one.
    /// Returns false if it's a duplicate, or if growing the table failed.
    bool insert(string_view str, Value value);

    /// Returns the existing vlue for this key; else calls the `creator` function,
    /// which should return `Value`, and adds the resulting value.
    /// Returns null if the creator function returned null, or if growing the table failed.
    template <typename FN>
    Value findOrInsert(string_view str, FN creator) {
        int32_t hashCode = computeHash(str);
        if (auto [entry, symbol] = search(str, hashCode); symbol) {
            return symbol;
        } else {
            symbol = creator(*_heap);
            if (!insert(str, hashCode, entry, symbol))
                symbol = nullvalue;
            return symbol;
        }
    }

    using Visitor = function_ref<bool(Value, uint32_t hash)>;

    /// Calls the `visitor` callback once with each Value (and its hash code.)
    bool visit(Visitor visitor) const;

    void dump(std::ostream&) const;

    //TODO: Generalize this
    string_view recoverKey(Value v) const {return v.as<Symbol>().str();}

private:
    struct HashEntry {
        Val hash;       // key's hash code (int)
        Val symbol;     // Value (null if none)
    };

    explicit HashTable(Heap &heap, Array array, bool recount);
    uint32_t tableSize() const                      {return _sizeMask + 1;}
    HashEntry* begin() const                        {return (HashEntry*)_array.begin();}
    HashEntry* end() const                          {return (HashEntry*)_array.end();}
    std::pair<HashEntry*,Value> search(string_view str, int32_t hashCode) const pure;
    bool insert(string_view str, int32_t hashCode, HashEntry* entry, Value);
    void recount();
    bool grow();

    Heap*       _heap;
    Handle<Array> _array;
    uint32_t    _count;
    uint32_t    _sizeMask;
    uint32_t    _capacity;
};


}
