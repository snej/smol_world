//
// Val.cc
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

#include "Val.hh"
#include "Value.hh"
#include <iostream>

namespace snej::smol {

Object Val::asObject() const {
    return Object(*this);
}


void Val::set(Block const* dst, Type type) {
    if (dst) {
        // Convert real pointer to offset:
        intptr_t off = intptr_t(dst) - intptr_t(this);
        assert(off <= INT32_MAX && off >= INT32_MIN); // receiver must be within 2GB of dst!
        _val = (uintpos(off) << TagSize) | uintpos(type);
        assert(isObject()); // offsets of 0,1,2,3 conflict with values null,nullish,false,true
    } else {
        _val = NullVal;
    }
}


Val& Val::operator= (Val const& val) {
    if (val.isObject())
        set(val._block(), val.rawType()); // converts to true pointer then back to relative
    else
        _val = val._val;
    return *this;
}


Val& Val::operator= (Value value) {
    if (value.isObject())
        set(value._block(), value.rawType()); // converts to true pointer then back to relative
    else
        _val = uintpos(value._val);
    return *this;
}


const char* TypeName(Type t) {
    static constexpr const char* kTypeNames[int(Type::Max)+1] = {
        "null", "int", "float", "string", "symbol", "blob", "array", "dict", "bool"
    };
    if (t > Type::Max) return "!BAD_TYPE!";
    return kTypeNames[uint8_t(t)];
}

std::ostream& operator<<(std::ostream& out, Type t) {return out << TypeName(t);}


}
