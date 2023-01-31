//
// SymbolTable.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include <functional>
#include <iosfwd>
#include <memory>

namespace snej::smol {

class SymbolTable {
public:
    /// Creates a new empty SymbolTabe for a Heap.
    static std::unique_ptr<SymbolTable> create(Heap *heap);

    /// Constructs a SymbolTable for a Heap, with an already existing array backing store.
    SymbolTable(Heap *heap, Array array)
    :_heap(heap)
    ,_table(array)
    ,_count(_table.count())
    { }

    /// The number of symbols in the table
    uint32_t size() const                               {return _count;}

    /// Returns the existing symbol with this string, or nothing.
    Maybe<Symbol> find(std::string_view s) const;

    /// Returns the existing symbol with this string, or creates a new one.
    Maybe<Symbol> create(std::string_view s);

    using Visitor = std::function<bool(Symbol, uint32_t hash)>;
    /// Calls the `visitor` callback once with each Symbol (and its hash code.)
    bool visit(Visitor visitor) const;

    friend std::ostream& operator<<(std::ostream& out, SymbolTable const &st) {
        st._table.dump(out); return out;
    }

protected:
    friend class Heap;
    void setHeap(Heap* h)                               {_heap = h;}

private:
    struct HashEntry {
        Val hash;       // symbol string's hash code (int)
        Val symbol;     // Symbol object or null
    };

    struct HashTable {
        Array       array;
        uint32_t    sizeMask, capacity;

        explicit HashTable(Array array);
        HashEntry* begin() const                        {return (HashEntry*)array.begin();}
        HashEntry* end() const                          {return (HashEntry*)array.end();}
        uint32_t tableSize() const                      {return sizeMask + 1;}
        uint32_t count() const pure;
        std::pair<HashEntry*,Maybe<Symbol>> search(string_view str, int32_t hashCode) const pure;
        void dump(std::ostream&) const;
    };

    bool grow();

    Heap*       _heap = nullptr;
    HashTable   _table;
    uint32_t    _count = 0;
};

}
