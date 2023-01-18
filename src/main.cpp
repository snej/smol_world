//
//  main.cpp
//  DataWorld
//
//  Created by Jens Alfke on 1/12/23.
//

#include "smol_world.hh"
#include <iostream>
#include <string>

using namespace std;


int main(int argc, const char * argv[]) {
    string persisted;
    {
        Heap heap(100000);
        UsingHeap u(heap);
        
        auto &arr = *Array::create(4, &heap);
        assert(arr.count() == 4);
        cout << arr << endl;
        heap.setRoot(&arr);
        assert(heap.rootObject()->is<Array>());
        assert(heap.rootObject()->as<Array>() == &arr);

        arr[0] = 1234;
        arr[1] = -4567;

        auto str = String::create("Cowabunga!", heap);
        cout << str << endl;
        assert(str->count() == 10);
        assert(str->get() == "Cowabunga!");
        arr[2] = str->asVal(heap);
        arr[3] = str->asVal(heap);

        String::create("Garbage!", heap);

        assert(str->get() == "Cowabunga!");

        cout << arr << endl;

        int n = 0;
        cout << "Contents:\n";
        heap.visit([&](Object const* obj) {
            n++;
            cout << "\t" << uintpos(heap.pos(obj)) << ": " << obj << std::endl;
            return true;
        });
        assert(n == 2);

        n = 0;
        cout << "Contents again:\n";
        heap.visit([&](Object const* obj) {
            n++;
            cout << "\t" << uintpos(heap.pos(obj)) << ": " << obj << std::endl;
            return true;
        });
        assert(n == 2);

        persisted = string((char*)heap.base(), heap.used());
    }

    cout << "Saved as " << persisted.size() << " bytes.\n";

    {
        Heap heap = Heap::existing(persisted.data(), persisted.size(), 100000);
        UsingHeap u(heap);
        cout << "Root is " << heap.rootVal() << endl;
        assert(heap.rootObject()->is<Array>());
        Array *root = heap.root<Array>();
        assert(root);
        assert(heap.root<Dict>() == nullptr);
        cout << root << "... at " << (void*)root << endl;

        String *str = (*root)[2].asString(heap);
        cout << "String: " << str << "... at " << (void*)str << endl;

        cout << "before GC: " << heap.used() << " bytes\n";
        {
            GarbageCollector gc(heap);
            gc.update(&str);
        }
        cout << "after GC: " << heap.used() << " bytes\n";
        cout << "Now string is " << str << "... at " << (void*)str << endl;
    }

    return 0;
}
