//
// Collections.cc
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

#include "Collections.hh"
#include <iomanip>
#include <iostream>


struct FakeEntry {
    ValBase key;
    ValBase value;
};

static bool fakeKeyCmp(FakeEntry const& a, FakeEntry const& b) {
    return a.key.rawBits() > b.key.rawBits();
}

static inline bool operator==(Val val, Value value) {
    return val.rawBits() == value.asValBase().rawBits();
}


// Returns the DictEntry with this key, or else the pos where it should go (DictEntry with next higher key),
// or else the end.
static DictEntry* _findEntry(slice<DictEntry> entries, Value key) {
    FakeEntry target = {key.asValBase(), nullval};
    return (DictEntry*) std::lower_bound((FakeEntry*)entries.begin(),
                                         (FakeEntry*)entries.end(),
                                         target, fakeKeyCmp);
}


void Dict::sort(size_t count) {
    std::sort((FakeEntry*)begin(), (FakeEntry*)begin() + count, fakeKeyCmp);
}


slice<DictEntry> Dict::items() const {
    slice<DictEntry> all = allItems();
    return {all.begin(), _findEntry(all, nullvalue)};
}


Val* Dict::find(Value key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep != all.end() && ep->key.rawBits() == key.asValBase().rawBits())
        return &ep->value;
    else
        return nullptr;
}


bool Dict::set(Value key, Value value, bool insertOnly) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep == all.end()) {
        return false;   // not found, and would go after last item (so dict must be full)
    } else if (ep->key == key) {
        if (insertOnly) return false;
        ep->value = value;
        return true;
    } else if (all.back().key == nullval) {
        ::memmove(ep + 1, ep, (all.end() - ep - 1) * sizeof(DictEntry));
        (Val&)ep->key = key;
        ep->value = value;
        return true;
    } else {
        return false; // not found, but no room to insert
    }
}


bool Dict::replace(Value key, Value newValue) {
    if (Val *valp = find(key)) {
        *valp = newValue;
        return true;
    } else {
        return false;
    }
}


bool Dict::remove(Value key) {
    slice<DictEntry> all = allItems();
    if (DictEntry *ep = _findEntry(all, key); ep != all.end() && ep->key == key) {
        ::memmove(ep, ep + 1, (all.end() - (ep + 1)) * sizeof(DictEntry));
        new (&all.back()) DictEntry {};
        return true;
    } else {
        return false;
    }
}



std::ostream& operator<<(std::ostream& out, Null const&) {
    return out << "null";
}

std::ostream& operator<<(std::ostream& out, Int const& val) {
    return out << val.asInt();
}

std::ostream& operator<<(std::ostream& out, Bool const& val) {
    return out << (val ? "true" : "false");
}

std::ostream& operator<<(std::ostream& out, String const& str) {
    out << "“" << str.str() << "”";
    return out;
}

std::ostream& operator<<(std::ostream& out, Symbol const& str) {
    out << "«" << str.str() << "»";
    return out;
}

std::ostream& operator<<(std::ostream& out, Blob const& blob) {
    out << "Blob<" << std::hex;
    for (byte b : blob) {
        out << std::setw(2) << unsigned(b);
    }
    out << std::dec << ">";
    return out;
}

std::ostream& operator<<(std::ostream& out, Array const& arr) {
    out << "Array[" << arr.count();
    if (!arr.empty()) {
        out << ": ";
        int n = 0;
        for (Val const& val : arr) {
            if (n++) out << ", ";
            out << val;
        }
    }
    return out << "]";
}

std::ostream& operator<<(std::ostream& out, Dict const& dict) {
    out << "Dict{" << dict.count();
    int n = 0;
    for (auto &entry : dict) {
        if (n++) out << ", ";
        out << entry.key << ": " << entry.value;
    }
    return out << "}";
}

std::ostream& operator<< (std::ostream& out, Value const& val) {
    val.visit([&](auto t) {out << t;});
    return out;
}
