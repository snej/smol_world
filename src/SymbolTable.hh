//
// SymbolTable.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include <functional>
#include <iosfwd>


class SymbolTable {
public:
    explicit SymbolTable(Heap *heap, Val table)
    :_heap(heap)
    ,_table(nullptr)
    {
        setTable(table);
    }

    uint32_t count() const                              {return _count;}
    
    Symbol find(std::string_view s) const;
    Symbol create(std::string_view s);

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
        std::pair<HashEntry*,Symbol> search(Heap*, string_view str, int32_t hashCode) const;
        void dump(std::ostream&, Heap const*) const;
    };

    void setHeap(Heap* h)                               {_heap = h;}
    void setTable(Val newTable);
    void setTable(Array table);
    bool grow();

    HashTable   _table;
    Heap*       _heap = nullptr;
    uint32_t    _count = 0;
};

