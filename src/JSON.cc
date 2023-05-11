//
// JSON.cc
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

#include "JSON.hh"
#include "HashTable.hh"
#include <deque>
#include <iostream>
#include "rapidjson/error/en.h"
#include "rapidjson/reader.h"
#include "rapidjson/writer.h"


namespace snej::smol {
using namespace std;


#pragma mark - PARSING JSON:


static inline heapsize grow(heapsize size) {
    return size + (size >> 1); // x 1.5
}


class JSONParseHandler {
public:
    static constexpr size_t kMaxStringDedupSize = 1000;

    JSONParseHandler(Heap &h)
    :_heap(h)
    ,_root(h)
    ,_emptyArray(h)
    ,_strings(h, 100)
    { }

    ~JSONParseHandler() {
        //std::cout << "JSONParseHandler created " << _strings.count() << " short strings, of " << _numShortStrings << " in the JSON. Total strings is " << _numStrings << "\n";
    }

    Value root()            {return _root;}

    bool Null()             { return addValue(nullishvalue); }
    bool Bool(bool b)       { return addValue(smol::Bool(b)); }
    bool Int(int i)         { return addNumber(i); }
    bool Uint(unsigned u)   { return addNumber(u); }
    bool Int64(int64_t i)   { return addNumber(i); }
    bool Uint64(uint64_t u) { return addNumber(u); }
    bool Double(double d)   { return addNumber(d); }

    bool RawNumber(const char*, rapidjson::SizeType, bool /*copy*/) {return false;}

    bool String(const char* str, rapidjson::SizeType length, bool /*copy*/) {
        ++_numStrings;
        Value string;
        if (length <= kMaxStringDedupSize) {
            ++_numShortStrings;
            string = _strings.findOrInsert(string_view(str,length), [&](Heap& heap) {
                return newString({str, length}, heap);
            });
        } else {
            string = newString({str, length}, _heap);
        }
        return addValue(string);
    }

    bool StartArray() {
        unless(vec, newArray(4, _heap)) {return false;}
        _stack.emplace_back(vec);
        return true;
    }
    bool EndArray(rapidjson::SizeType /*elementCount*/) {
        Handle<Array> vec = _stack.back().as<Array>();
        _stack.pop_back();
        if (vec.empty()) {
            // Empty arrays are common in JS; use a singleton to save room.
            if (!_emptyArray) {
                _emptyArray = newArray(0, _heap);
                if (!_emptyArray)
                    return false;
            }
            return addValue(_emptyArray);
        } else {
            compact(vec);
            return addValue(vec);
        }
    }

    bool StartObject() {
        unless(dict, newDict(4, _heap)) {return false;}
        _stack.emplace_back(dict);
        return true;
    }

    bool Key(const char* str, rapidjson::SizeType length, bool /*copy*/) {
        unless(key, newSymbol({str, length}, _heap)) {return false;}
        _keys.push_back(key);
        return true;
    }

    bool EndObject(rapidjson::SizeType /*memberCount*/) {
        Handle<Dict> dict = _stack.back().as<Dict>();
        _stack.pop_back();
        compact(dict);
        return addValue(dict);
    }

private:
    bool addValue(Value val) {
        if (!val) {
            return false;
        } else if (_stack.empty()) {
            assert(!_root);
            _root = val;
        } else {
            Object obj = _stack.back();
            if_let (vec, obj.maybeAs<Array>()) {
                return append(vec, val);
            } else {
                assert(!_keys.empty());
                auto key = _keys.back();
                _keys.pop_back();
                return insert(obj.as<Dict>(), key, val);
            }
        }
        return true;
    }

    bool addNumber(auto num)  {
        return addValue(newNumber(num, _heap));
    }

    bool append(Array vec, Value val) {
        Handle vecHandle(&vec);   // in case grow() triggers GC
        Handle valHandle(&val);   // in case grow() triggers GC
        if (!vec.append(val)) {
            unless(newArray, _heap.grow(vec, grow(vec.capacity()))) {return false;}
            __unused bool ok = newArray.append(val);
            assert(ok);
            _stack.back() = newArray;
        }
        return true;
    }

    bool insert(Dict dict, Symbol key, Value val) {
        Handle dictHandle(&dict);   // in case grow() triggers GC
        Handle keyHandle(&key);   // in case grow() triggers GC
        Handle valHandle(&val);
        if (!dict.insert(key, val)) {
            unless(newDict, _heap.grow(dict, grow(dict.capacity()))) {return false;}
            if (!newDict.insert(key, val))
                return false;
            _stack.back() = newDict;
        }
        return true;
    }

    void compact(Array &vec) {
        if (!vec.full()) {
            if_let(newVec, _heap.grow(vec, vec.itemCount())) {
                vec = newVec;
            }
        }
    }

    void compact(Dict &dict) {
        if (!dict.full()) {
            //TODO: Implement grow() for Dict
            if_let(d, newDict(dict.size(), _heap)) {
                for (auto &item : dict.items())
                    d.set(item.key.as<Symbol>(), item.value);
                dict = d;
            }
        }
    }

    Heap& _heap;
    Handle<Value> _root;
    deque<Handle<Object>> _stack;
    deque<Handle<Symbol>> _keys;
    Handle<Maybe<Array>> _emptyArray;
    HashSet _strings;
    unsigned _numStrings = 0, _numShortStrings = 0;
};

Value newFromJSON(string const& json, Heap &heap, string* outError) {
    UsingHeap u(heap);
    rapidjson::StringStream in(json.c_str());
    rapidjson::Reader reader;
    JSONParseHandler handler(heap);
    auto result = reader.Parse(in, handler);
    if (result.IsError()) {
        if (outError)
            *outError = rapidjson::GetParseError_En(result.Code());
        return nullvalue;
    } else {
        return handler.root();
    }
}

Value newFromJSON(string_view json, Heap &heap, string* outError) {
    return newFromJSON(string(json), heap, outError);
}


#pragma mark - CONVERTING TO JSON:


static bool writeVal(Value val, rapidjson::Writer<rapidjson::StringBuffer> &writer) {
    switch (val.type()) {
        case Type::Null:    return writer.Null();
        case Type::Bool:    return writer.Bool(val.asBool());
        case Type::Int:     return writer.Int(val.asInt());
#ifdef BIGINT
        case Type::BigInt:  return writer.Int64(val.as<BigInt>().asInt());
#endif
        case Type::Float:   return writer.Double(val.as<Float>().asDouble());

        case Type::String: {
            string_view str = val.as<String>().str();
            return writer.String(str.data(), uint32_t(str.size()));
        }
        case Type::Symbol: {
            string_view str = val.as<Symbol>().str();
            return writer.String(str.data(), uint32_t(str.size()));
        }
        case Type::Array: {
            if (!writer.StartArray()) return false;
            for (Value item : val.as<Array>().items()) {
                if (!item)
                    break; // stop at a true null (which can't be written anyway)
                if (!writeVal(item, writer)) return false;
            }
            return writer.EndArray();
        }
#ifdef VECTOR
        case Type::Vector: {
            if (!writer.StartArray()) return false;
            for (Value item : val.as<Vector>().items())
                if (!item || !writeVal(item, writer)) return false;
            return writer.EndArray();
        }
#endif
        case Type::Dict: {
            if (!writer.StartObject()) return false;
            for (DictEntry const& item : val.as<Dict>().items()) {
                if (item.value) {
                    string_view key = item.key.as<Symbol>().str();//assumes key is a Symbol
                    if (!writer.Key(key.data(), uint32_t(key.size()))) return false;
                    if (!writeVal(item.value, writer)) return false;
                }
            }
            return writer.EndObject();
        }
        default:
            return false;
    }
}


std::string toJSON(Value val) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer writer(buf);
    if (!writeVal(val, writer))
        return "";
    return buf.GetString();
}


}
