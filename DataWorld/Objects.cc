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

