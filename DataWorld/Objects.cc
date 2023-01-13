//
// Objects.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "Objects.hh"
#include <iostream>


// just like DictEntry except `key` isn't const, allowing it to be swapped/assigned.
struct MutEntry {
    Val key;
    Val value;
    operator DictEntry&() {return *(DictEntry*)this;}    // so that Dict::keyCmp will work
};

void Dict::sort(size_t count) {
    std::sort((MutEntry*)begin(), (MutEntry*)begin() + count, Dict::keyCmp);
}


// Returns the DictEntry with this key, or else the pos where it should go (DictEntry with next higher key),
// or else the end.
DictEntry* Dict::_findEntry(Val keyStr) {
    return std::lower_bound(begin(), endAll(), DictEntry{keyStr, nullval}, Dict::keyCmp);
}


Val* Dict::find(Val keyStr) {
    if (DictEntry *ep = _findEntry(keyStr); ep != endAll() && ep->key == keyStr)
        return &ep->value;
    else
        return nullptr;
}


bool Dict::set(Val keyStr, Val value) {
    if (DictEntry *ep = _findEntry(keyStr); ep == endAll()) {
        return false; // not found and full!
    } else if (ep->key == keyStr) {
        ep->value = value;
        return true;
    } else {
        assert(!full());
        ::memmove(ep + 1, ep, (endAll() - ep - 1) * sizeof(DictEntry));
        new (ep) DictEntry {keyStr, value};
        return true;
    }
}


bool Dict::replace(Val keyStr, Val newValue) {
    if (Val *valp = find(keyStr)) {
        *valp = newValue;
        return true;
    } else {
        return false;
    }
}


bool Dict::remove(Val keyStr) {
    if (DictEntry *ep = _findEntry(keyStr)) {
        ::memmove(ep, ep + 1, (endAll() - ep) * sizeof(DictEntry));
        new (ep) DictEntry {nullval, nullval};
        return true;
    } else {
        return false;
    }
}



std::ostream& operator<<(std::ostream& out, String const* str) {
    out << "String[“" << str->get() << "”]";
    return out;
}

std::ostream& operator<<(std::ostream& out, Array const* arr) {
    out << "Array[" << arr->count();
    if (!arr->empty()) {
        out << ": ";
        int n = 0;
        for (Val val : *arr) {
            if (n++) out << ", ";
            out << val;
        }
    }
    return out << "]";
}
