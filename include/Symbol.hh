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
    static Symbol* create(const char *str, size_t size, IN_MUT_HEAP);
    static Symbol* create(string_view s, IN_MUT_HEAP) {return create(s.data(), s.size(), heap);}

    /// Looks up an existing Symbol. Returns nullptr if it's not registered in this heap.
    static Symbol* find(const char *str, size_t size, IN_HEAP);
    static Symbol* find(string_view s, IN_HEAP) {return find(s.data(), s.size(), heap);}

    const char* data() const        {return begin();}
    string_view get() const         {auto i = items(); return {i.begin(), i.size()};}

private:
    explicit Symbol(heapsize capacity)   :Collection(capacity) { }
    Symbol(const char *str, size_t size) :Collection(str, size) { }
};
