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


int xmain(int argc, const char * argv[]) {
    string persisted;
    {
        Heap heap(100000);
        UsingHeap u(heap);
        
        Array arr = Array::create(4, &heap).value();
        assert(arr.count() == 4);
        cout << arr << endl;
        heap.setRoot(arr);
        assert(heap.rootValue().is<Array>());
        assert(heap.rootValue().as<Array>() == arr);

        arr[0] = 1234;
        arr[1] = -4567;

        auto str = String::create("Cowabunga!", heap).value();
        cout << str << endl;
        assert(str.count() == 10);
        assert(str.get() == "Cowabunga!");
        arr[2] = str;
        arr[3] = str;

        String::create("Garbage!", heap);

        assert(str.get() == "Cowabunga!");

        cout << arr << endl;

        auto objectPos = [&](Object const& obj) {
            return uintpos(heap.pos(obj.rawBytes().begin()));
        };

        int n = 0;
        cout << "Contents:\n";
        heap.visit([&](Object obj) {
            n++;
            if (n == 1) assert(obj == arr);
            if (n == 2) assert(obj == str);
            cout << "\t" << objectPos(obj) << ": " << obj << std::endl;
            return true;
        });
        assert(n == 2);

        n = 0;
        cout << "Contents again:\n";
        heap.visit([&](Object obj) {
            n++;
            cout << "\t" << objectPos(obj) << ": " << obj << std::endl;
            return true;
        });
        assert(n == 2);

        persisted = string((char*)heap.base(), heap.used());
    }

    cout << "Saved as " << persisted.size() << " bytes.\n";

#if 0
    {
        cout << "\nReloading...\n";
        Heap heap = Heap::existing(persisted.data(), persisted.size(), 100000);
        assert(heap.valid());
        UsingHeap u(heap);
        cout << "Root is " << heap.rootVal() << endl;
        assert(heap.rootValue().is<Array>());
        Array *arr = heap.root<Array>();
        assert(arr);
        assert(heap.root<Dict>() == nullptr);
        cout << arr << "... at " << (void*)arr << endl;

        String str = (*arr)[2].as<String>(heap);
        cout << "String: " << str << "... at " << (void*)str << endl;

        cout << "before GC: " << heap.used() << " bytes\n";
        {
            GarbageCollector gc(heap);
            gc.update(&str);
            gc.update(&arr);
        }
        cout << "after GC: " << heap.used() << " bytes\n";
        cout << "Now array is " << arr << "... at " << (void*)arr << endl;
        assert(arr.is<Array>());
        assert(arr.count() == 4);
        cout << "Now string is " << str << "... at " << (void*)str << endl;
        assert(str.is<String>());
        assert(str.count() == 10);
        assert(str.get() == "Cowabunga!");
    }
#endif
    return 0;
}

