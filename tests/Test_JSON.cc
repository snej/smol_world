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


static void checkHeap(Heap &heap) {
    if (!heap.validate())
        FAIL("Corrupt heap: " << heap.invalid());
}


TEST_CASE("JSON", "[object],[json]") {
    Heap heap(4000);
    UsingHeap u(heap);
    GarbageCollector::runOnDemand(heap);

    string err;
    Handle<Value> v = newFromJSON(readFile(JSON_TEST_DATA_DIR "svg_menu.json"), heap, &err);
    INFO("Error is " << err);
    REQUIRE(v);
    Dict dict = v.as<Dict>();
    CHECK(dict.size() == 1);
    auto menu = dict.get(newSymbol("menu", heap).value());
    REQUIRE(menu.type() == Type::Dict);
    auto items = menu.as<Dict>().get(newSymbol("items", heap).value());
    REQUIRE(items.type() == Type::Array);
    CHECK(items.as<Array>().itemCount() == 22);
}


static void testReadJSON(const char *path) {
    Heap heap(1000000);
    heap.makeIterable();
    UsingHeap u(heap);
    GarbageCollector::runOnDemand(heap);

    string err;
    Handle<Value> v = newFromJSON(readFile(path), heap, &err);
    {
        INFO("Error is '" << err << "'");
        REQUIRE(v);
    }
    Dict dict = v.as<Dict>();
    heap.setRoot(dict);

    heap.dump(cout);
    checkHeap(heap);

    string json = toJSON(v);
    if (json.size() < 10000)
        cout << "As JSON: " << json << endl;

    auto numSymbols = heap.symbolTable().size();
//  TEMP  heap.dropSymbolTable();

    cout << "Heap space used is " << heap.used() << " bytes; JSON is " << json.size()
         << " ... " << (heap.used() / double(json.size()) * 100.0) << "%\n";

    cout << "\n..... garbage collection .....\n\n";
    GarbageCollector::run(heap);

    CHECK(heap.validate());
    heap.dump(cout);
    heap.dumpBlockSizes(cout);
    heap.dumpStrings(cout);
    checkHeap(heap);
    CHECK(heap.root() != nullvalue);

    string json2 = toJSON(v);
    if (json2.size() < 10000)
        cout << "As JSON: " << json2 << endl;
    cout << "Heap space used is " << heap.used() << " bytes; JSON is " << json.size()
         << " ... " << (heap.used() / double(json.size()) * 100.0) << "%\n";
    REQUIRE(json2.size() == json.size());
    // TODO: Compare equality; that will require canonical Dict ordering

    cout << "Recovered " << heap.symbolTable().size() << " Symbols.\n";
    cout << heap.symbolTable();
    CHECK(heap.symbolTable().size() == numSymbols);
}


TEST_CASE("Read Small JSON", "[object],[json]") {
    testReadJSON(JSON_TEST_DATA_DIR "svg_menu.json");
}


TEST_CASE("Read Med JSON", "[object],[json]") {
    testReadJSON(JSON_TEST_DATA_DIR "update-center.json");
}


TEST_CASE("Read Large JSON", "[object],[json]") {
    testReadJSON(JSON_TEST_DATA_DIR "twitter.json");
}
