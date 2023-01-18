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
#include "Object.hh"
#include <iostream>


Type Val::type(IN_HEAP) const {
    if (isInt())
        return Type::Int;
    else if (isNull())
        return Type::Null;
    else
        return asObject(heap)->type();
}


const char* TypeName(Type t) {
    static constexpr const char* kTypeNames[10] = {
        "bignum", "string", "symbol", "blob",
        "array", "dict", "?spare1?", "?spare2?",
        "null", "int"
    };
    return kTypeNames[uint8_t(t)];
}


std::ostream& operator<<(std::ostream& out, Val val) {
    if (auto heap = Heap::current()) {
        switch(auto type = val.type(CUR_HEAP)) {
            case Type::Null:
                out << "null";
                break;
            case Type::Int:
                out << val.asInt();
                break;
            default:
                out << TypeName(type) << "@" << uintpos(val.asPos());
                break;
        }
    } else {
        // No current heap...
        if (val.isInt())
            out << val.asInt();
        else
            out << "???@" << uintpos(val.asPos());
    }
    return out;
}
