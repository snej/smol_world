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


static const char* kTypeNames[6] = {
    "string", "array", "dict", "???", "null", "int"
};


std::ostream& operator<<(std::ostream& out, Val val) {
    auto type = val.type();
    switch(type) {
        case ValType::Null:
            out << "null";
            break;
        case ValType::Int:
            out << val.asInt();
            break;
        default:
            out << kTypeNames[int(type)] << "@" << val.asPos();
            break;
    }
    return out;
}
