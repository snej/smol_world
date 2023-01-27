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


Val::Val(Object const& obj, IN_HEAP)     :Val(obj.block(), heap) { }

Object Val::asObject(IN_HEAP) const      {return Object(*this, heap);}


Type Val::_type() const {
    if (isInt())
        return Type::Int;
    else if (isNull())
        return Type::Null;
    else if (isBool())
        return Type::Bool;
    else {
        assert(!isObject());    // will fail
        return Type::String;
    }
}


Type Val::type(IN_HEAP) const {
    if (isInt())
        return Type::Int;
    else if (_val > FalseVal)
        return asBlock(heap)->type();
    else if (isNull())
        return Type::Null;
    else
        return Type::Bool;
}


const char* TypeName(Type t) {
    static constexpr const char* kTypeNames[11] = {
        "bignum", "string", "symbol", "blob",
        "array", "dict", "?spare1?", "?spare2?",
        "null", "bool", "int"
    };
    if (uint8_t(t) >= 11) return "!BAD_TYPE!";
    return kTypeNames[uint8_t(t)];
}

std::ostream& operator<<(std::ostream& out, Type t) {return out << TypeName(t);}


std::ostream& operator<<(std::ostream& out, Val val) {
    if (val.isNull()) {
        out << "null";
    } else if (val.isBool()) {
        out << (val.asBool() ? "true" : "false");
    } else if (val.isInt()) {
        out << val.asInt();
    } else if (auto heap = Heap::current()) {
        out << TypeName(val.type(heap)) << "@" << uintpos(val.asPos());
    } else {
        out << "???@" << std::hex << val.rawBits() << std::dec;
    }
    return out;
}
