//
// Symbol.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"


class Symbol : public Collection<Symbol, char, Type::Symbol> {
public:
    /// Returns a Symbol object containing the given string.
    /// If one already exists in this heap, returns it; else creates a new one.
    static Symbol* create(string_view s, IN_MUT_HEAP);

    /// Looks up an existing Symbol. Returns nullptr if it's not registered in this heap.
    static Symbol* find(string_view, IN_HEAP);

    using Visitor = std::function<bool(Symbol*, uint32_t bucket)>;
    static void visitSymbols(IN_HEAP, Visitor);

    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;

    explicit Symbol(heapsize capacity)   :Collection(capacity) { }
    Symbol(size_t cap, const char *str, size_t size) :Collection(cap, str, size) { }
    static Array* getTable(IN_HEAP);
};
