//
// Test_RelPtr.cc
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

#include "smol_world.hh"
#include "catch.hpp"
#include <iostream>

using namespace std;



template <typename T>
class RelPtr {
public:
    RelPtr()                            :_off(0) { }
    explicit RelPtr(T *dst)             {*this = dst;}

    T* get() const                      {return _off ? (T*)((byte*)this + _off) : nullptr;}
    operator T*() const                 {return get();}
    T& operator*() const                {return *get();}
    T* operator->() const               {return get();}

    RelPtr& operator=(T *dst) {
        if (dst) {
            auto off = intptr_t(dst) - intptr_t(this);
            assert(off <= INT32_MAX && off >= INT32_MIN);
            _off = int32_t(off);
        } else {
            _off = 0;
        }
        return *this;
    }

    RelPtr& operator=(RelPtr const& src) {return *this = src.get();}

private:
    RelPtr(RelPtr const& src) = delete;

    int32_t _off;
};


struct Foo {
    int i1 = 111;
    RelPtr<int> p1;
    char filler[1000];
    RelPtr<int> p2;
    int i2 = 222;
};



TEST_CASE("RelPtr") {
    auto foo = make_unique<Foo>();
    CHECK(foo->p1 == nullptr);
    foo->p1 = &foo->i1;
    CHECK(foo->p1.get() == &foo->i1);
    CHECK(foo->p1 == &foo->i1);
    CHECK(*foo->p1 == 111);
    foo->p1 = &foo->i2;
    CHECK(foo->p1.get() == &foo->i2);
    CHECK(*foo->p1 == 222);

    const void* loc1 = foo->p1;
    CHECK(loc1 == &foo->i2);

    CHECK(foo->p1 != foo->p2);

    foo->p2 = foo->p1;
    CHECK(foo->p1 == foo->p2);
}
