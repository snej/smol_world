//
// Symbol.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"


class Symbol : public Collection<Symbol, char, Type::Symbol> {
public:
    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    template <class T, typename ITEM, Type TYPE> friend class Collection;
    friend class SymbolTable;

    static Symbol* actuallyCreate(string_view str, IN_MUT_HEAP) {
        return Collection::create(str.data(), str.size(), heap);
    }
    Symbol(size_t cap, const char *str, size_t size) :Collection(cap, str, size) { }
};
