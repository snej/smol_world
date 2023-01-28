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


class SymbolTable {
public:
    static std::unique_ptr<SymbolTable> create(IN_MUT_HEAP);

    SymbolTable(Heap *heap, Array array)
    :_heap(heap)
    ,_table(array)
    ,_count(_table.count())
    { }

    uint32_t count() const                              {return _count;}
    
    Maybe<Symbol> find(std::string_view s) const;
    Maybe<Symbol> create(std::string_view s);

    using Visitor = std::function<bool(Symbol, uint32_t hash)>;
    bool visit(Visitor visitor) const;

    friend std::ostream& operator<<(std::ostream& out, SymbolTable const &st) {
        st._table.dump(out, st._heap); return out;
    }
private:
    friend class Heap;

    struct HashEntry {
        Val hash;
        Val symbol;
    };
    struct HashTable {
        HashEntry *begin, *end;
        uint32_t sizeMask, capacity;
        Array array;

        explicit HashTable(Array array);
        uint32_t size() const  {return sizeMask + 1;}
        explicit operator bool() const  {return begin != nullptr;}
        uint32_t count() const;
        std::pair<HashEntry*,Maybe<Symbol>> search(Heap*, string_view str, int32_t hashCode) const;
        void dump(std::ostream&, Heap const*) const;
    };

    void setHeap(Heap* h)                               {_heap = h;}
    void setTable(Val const& newTable);
    bool grow();

    Heap*       _heap = nullptr;
    HashTable   _table;
    uint32_t    _count = 0;
};

