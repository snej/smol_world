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
#include <iostream>


static constexpr std::array<ValType,16> tagTypeFn() {
    std::array<ValType,16> types { };
    for (Val::TagBits t = Val::IntTag; t <= Val::DictTag; t = Val::TagBits(t+1)) {
        for (int i = 0; i < 16; i += 8)
            types[i + t] = ValType(t - Val::IntTag + int(ValType::Int));
    }
    types[Val::NullVal] = ValType::Null;
    types[Val::FalseVal] = ValType::Bool;
    return types;
}

const std::array<ValType,16> Val::kTagType = tagTypeFn();


static const char* kTypeNames[9] = {
    "string", "array", "dict", "?1?", "int", "?2?", "?3?", "?4?", "null"
};


std::ostream& operator<<(std::ostream& out, Val val) {
    switch(auto type = val.type()) {
        case ValType::Null:
            out << "null";
            break;
        case ValType::Int:
            out << val.asInt();
            break;
        default:
            out << kTypeNames[int(type)] << "@" << uintpos(val.asPos());
            break;
    }
    return out;
}
