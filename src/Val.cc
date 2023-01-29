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


Val::Val(Object const& obj)     :Val(obj.block()) { }

Object Val::asObject() const      {return Object(*this);}

Val& Val::operator= (Val const& value) {
    if (value.isObject())
        *this = value.block();
    else
        _val = value.rawBits();
    return *this;
}

Val& Val::operator= (Value value) {
    if (value.isObject())
        *this = value.block();
    else
        _val = value.asValBase().rawBits();
    return *this;
}


bool operator==(Val const& val, Value const& value) {
    if (val.isObject())
        return value.isObject() && val.block() == value.block();
    else
        return !value.isObject() && val.rawBits() == value.asValBase().rawBits();
}


Type ValBase::_type() const {
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


Type Val::type() const {
    if (isInt())
        return Type::Int;
    else if (_val > FalseVal)
        return _block()->type();
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


std::ostream& operator<<(std::ostream& out, Val const& val) {
    switch (val.type()) {
        case Type::Null:    out << "null"; break;
        case Type::Bool:    out << (val.asBool() ? "true" : "false"); break;
        case Type::Int:     out << val.asInt(); break;
        default:            out << TypeName(val.type()) << "@" << (void*)val.block(); break;
    }
    return out;
}
