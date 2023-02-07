//
// Test_JSON.cc
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
#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>

using namespace std;
using namespace snej::smol;


#define JSON_TEST_DATA_DIR "./tests/data/"

static string readFile(const char *path) {
    INFO("Reading " << path);
    FILE *f=fopen(path,"rb");
    REQUIRE(f != NULL);
    fseek(f,0,SEEK_END);
    long len=ftell(f);
    fseek(f,0,SEEK_SET);
    string contents(len, 0);
    fread(contents.data(),1,len,f);
    fclose(f);
    return contents;
}


TEST_CASE("JSON", "[object],[json]") {
    Heap heap(100000);
    UsingHeap u(heap);

    string err;
    Handle<Value> v = newFromJSON(readFile(JSON_TEST_DATA_DIR "svg_menu.json"), heap, &err);
    REQUIRE(v);
    Dict dict = v.as<Dict>();
    heap.setRoot(dict);

    heap.dump(cout);

    CHECK(dict.size() == 1);
    auto menu = dict.get(newSymbol("menu", heap).value());
    REQUIRE(menu.type() == Type::Dict);
    auto items = menu.as<Dict>().get(newSymbol("items", heap).value());
    REQUIRE(items.type() == Type::Vector);
    CHECK(items.as<Vector>().size() == 22);

    string json = toJSON(v);
    cout << "As JSON: " << json << endl;

    cout << "Heap space used is " << heap.used() << " bytes; JSON is " << json.size() << ".\n";

    cout << "\n..... garbage collection .....\n\n";
    GarbageCollector::run(heap);

    heap.dump(cout);
    json = toJSON(v);
    cout << "As JSON: " << json << endl;
    cout << "Heap space used is " << heap.used() << " bytes; JSON is " << json.size() << ".\n";
}
