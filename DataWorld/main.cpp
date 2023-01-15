//
//  main.cpp
//  DataWorld
//
//  Created by Jens Alfke on 1/12/23.
//

#include "Heap.hh"
#include "Val.hh"
#include "Objects.hh"
#include <iostream>
#include <string>

using namespace std;


int main(int argc, const char * argv[]) {
    string persisted;
    {
        Heap heap(100000);
        auto &arr = *Array::create(4, &heap);
        assert(arr.count() == 4);
        cout << arr << endl;
        heap.setRoot(&arr);
        assert(heap.root().isArray());
        assert(heap.root().asArray(&heap) == &arr);

        arr[0] = 1234;
        arr[1] = -4567;

        auto str = String::create("Cowabunga!", &heap);
        cout << str << endl;
        assert(str->count() == 10);
        assert(str->get() == "Cowabunga!");
        arr[2] = str->asVal(&heap);
        arr[3] = str->asVal(&heap);

        String::create("Garbage!", &heap);

        cout << arr << endl;

        int n = 0;
        cout << "Contents:\n";
        heap.visit([&](Val val) {
            n++;
            cout << "\t" << val.asPos() << ": " << val << std::endl;
            return true;
        });
        assert(n == 2);

        n = 0;
        cout << "Contents again:\n";
        heap.visit([&](Val val) {
            n++;
            cout << "\t" << val.asPos() << ": " << val << std::endl;
            return true;
        });
        assert(n == 2);

        persisted = string((char*)heap.base(), heap.used());
    }

    cout << "Saved as " << persisted.size() << " bytes.\n";

    {
        Heap heap = Heap::existing(persisted.data(), persisted.size(), 100000);
        cout << "Root is " << heap.root() << endl;
        assert(heap.root().isArray());
        Array *root = heap.getRootAs<Array>();
        assert(root);
        cout << root << "... at " << (void*)root << endl;

        String *str = (*root)[2].asString(&heap);
        cout << "String: " << str << "... at " << (void*)str << endl;

        cout << "before GC: " << heap.used() << " bytes\n";
        {
            GarbageCollector gc(heap);
            gc.update(str);
        }
        cout << "after GC: " << heap.used() << " bytes\n";
        cout << "Now string is " << str << "... at " << (void*)str << endl;
    }

    return 0;
}
