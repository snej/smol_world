//
// SymbolTable.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include "HashTable.hh"
#include <functional>
#include <iosfwd>
#include <memory>

namespace snej::smol {

class SymbolTable {
public:
    /// Default initial capacity of hash table (number of symbols it can hold before growing.)
    static constexpr heapsize kInitialCapacity = 20;

    /// Creates a new empty SymbolTabe for a Heap.
    static std::unique_ptr<SymbolTable> create(Heap *heap, heapsize capacity = kInitialCapacity);

    /// Creates a new SymbolTabe that scans the Heap and adds all existing Symbol objects.
    static std::unique_ptr<SymbolTable> rebuild(Heap *heap);

    /// Constructs a SymbolTable for a Heap, with an already existing array backing store.
    SymbolTable(Heap *heap, Array array)                :_table(*heap, array) { }

    /// The number of symbols in the table
    uint32_t size() const                               {return _table.count();}

    /// Returns the existing symbol with this string, or nothing.
    Maybe<Symbol> find(std::string_view s) const        {return Maybe<Symbol>(_table.find(s));}

    /// Returns the existing symbol with this string, or creates a new one.
    Maybe<Symbol> create(std::string_view s);

    using Visitor = std::function<bool(Symbol)>;
    /// Calls the `visitor` callback once with each Symbol (and its hash code.)
    bool visit(Visitor visitor) const;

    friend std::ostream& operator<<(std::ostream& out, SymbolTable const &st) {
        st._table.dump(out, true); return out;
    }

protected:
    friend class Heap;
    void setHeap(Heap& h)                               {_table.setHeap(h);}

private:
    HashSet   _table;
};

}
