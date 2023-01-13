//
// Heap.cc
//

#include "Heap.hh"
#include "Val.hh"
#include "Objects.hh"

static constexpr uint32_t kMagic = 0xD217904A;

struct Header {
    uint32_t magic; // Must equal kMagic
    Val      root;  // Pointer to root object
};

static thread_local Heap* sCurHeap;


Heap::Heap(void *base, size_t capacity, bool malloced)
:_base((byte*)base)
,_end(_base + capacity)
,_cur(_base)
,_malloced(malloced)
{
    assert(isAligned(_base));
    assert(capacity >= sizeof(Header));
}

void Heap::reset() {
    _cur = _base;
    *alloc<Header>() = {kMagic, 0};
}


Heap Heap::existing(void *base, size_t used, size_t capacity) {
    Heap heap(base, capacity, false);
    heap._cur += used;
    auto header = (Header*)heap._base;
    if (header->magic != kMagic)
        throw std::runtime_error("Invalid Heap: wrong magic number");
    if (header->root.isPtr()) {
        heappos rootPos = header->root.asPos();
        if (rootPos < sizeof(Header) || rootPos >= used)
            throw std::runtime_error("Invalid Heap: bad root offset");
    }
    return heap;
}

bool Heap::validPos(heappos pos) const    {return pos >= sizeof(Header) && pos < used();}


Val Heap::root() const            {return ((Header*)_base)->root;}
void Heap::setRoot(Val val)       {((Header*)_base)->root = val;}

void Heap::enter()                  {assert(!sCurHeap); sCurHeap = this;}
void Heap::exit()                   {assert(sCurHeap == this); sCurHeap = nullptr;}
Heap* Heap::current()               {return sCurHeap;}


#pragma mark - GARBAGE COLLECTION:


GarbageCollector::GarbageCollector(Heap &heap)
:_tempHeap(std::make_unique<Heap>(heap.capacity()))
,_fromHeap(heap)
,_toHeap(*_tempHeap)
{
    scanRoot();
}


GarbageCollector::GarbageCollector(Heap &fromHeap, Heap &toHeap)
:_fromHeap(fromHeap), _toHeap(toHeap)
{
    _toHeap.reset();
    scanRoot();
}


void GarbageCollector::scanRoot() {
    _toHeap.setRoot(scanValue(_fromHeap.root()));
}


Val GarbageCollector::scanValue(Val val) {
    switch (val.type()) {
        case ValType::String: {
            auto str = val.asString().get(&_fromHeap);
            if (auto fwd = str->getForwardingAddress())
                return Ptr<String>(fwd);
            auto dstStr = String::create(str->get(), &_toHeap);
            Ptr dstPtr(dstStr, &_toHeap);
            str->setForwardingAddress(dstPtr);
            return dstPtr;
        }
        case ValType::Array: {
            auto array = val.asArray().get(&_fromHeap);
            if (auto fwd = array->getForwardingAddress())
                return Ptr<Array>(fwd);
            auto count = array->count();
            auto dstArray = Array::create(count, &_toHeap);
            Ptr dstPtr(dstArray, &_toHeap);
            auto iter = array->begin();
            array->setForwardingAddress(dstPtr);
            for (heapsize i = 0; i < count; ++i, ++iter) {
                (*dstArray)[i] = scanValue(*iter);
            }
            return dstPtr;
        }
        case ValType::Dict: {
            abort();//TODO
        }
        default:
            return val;
    }
}

void GarbageCollector::update(Val &val) {
    val = scanValue(val);
}



void Heap::garbageCollectTo(Heap &dstHeap) {
    GarbageCollector gc(*this, dstHeap);
}
