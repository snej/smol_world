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
    JSONParseHandler(Heap &h)
    :_heap(h)
    ,_root(h)
    ,_emptyString(h)
    ,_emptyArray(h)
    { }

    Value root()            {return _root;}

    bool Null()             { return addValue(nullishvalue); }
    bool Bool(bool b)       { return addValue(smol::Bool(b)); }
    bool Int(int i)         { return addInt(i); }
    bool Uint(unsigned u)   { return addInt(u); }
    bool Int64(int64_t i)   { return addInt(i); }
    bool Uint64(uint64_t u) { return u < INT64_MAX ? addInt(u) : addNumber(double(u)); }
    bool Double(double d)   { return addNumber(d); }

    bool RawNumber(const char*, rapidjson::SizeType, bool /*copy*/) {return false;}

    bool String(const char* str, rapidjson::SizeType length, bool /*copy*/) {
        if (length > 0) {
            unless(string, newString(str, length, _heap)) {return false;}
            return addValue(string);
        } else {
            if (!_emptyString) {
                _emptyString = newString("", 0, _heap);
                if (!_emptyString)
                    return false;
            }
            return addValue(_emptyString);
        }
    }

    bool StartArray() {
        unless(vec, newVector(4, _heap)) {return false;}
        _stack.emplace_back(vec);
        return true;
    }
    bool EndArray(rapidjson::SizeType /*elementCount*/) {
        Handle<Vector> vec = _stack.back().as<Vector>();
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
            return addValue(vec);
        }
    }

    bool StartObject() {
        unless(dict, newDict(4, _heap)) {return false;}
        _stack.emplace_back(dict);
        return true;
    }

    bool Key(const char* str, rapidjson::SizeType length, bool /*copy*/) {
        unless(key, newSymbol(str, length, _heap)) {return false;}
        _keys.push_back(key);
        return true;
    }

    bool EndObject(rapidjson::SizeType /*memberCount*/) {
        Handle<Dict> dict = _stack.back().as<Dict>();
        _stack.pop_back();
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
            if_let (vec, obj.maybeAs<Vector>()) {
                return append(vec, val);
            } else {
                assert(!_keys.empty());
                auto key = _keys.back();
                _keys.pop_back();
                return add(obj.as<Dict>(), key, val);
            }
        }
        return true;
    }

    bool addInt(int64_t num)    {return addValue(newInt(num, _heap));}
    bool addNumber(double num)  {return addValue(newNumber(num, _heap));}

    bool append(Vector vec, Value val) {
        if (!vec.append(val)) {
            Handle valHandle(&val);   // in case grow() triggers GC
            unless(newVector, _heap.grow(vec, grow(vec.capacity()))) {return false;}
            __unused bool ok = newVector.append(val);
            assert(ok);
            _stack.back() = newVector;
        }
        return true;
    }

    bool add(Dict dict, Symbol key, Value val) {
        if (!dict.insert(key, val)) {
            Handle keyHandle(&key);   // in case grow() triggers GC
            Handle valHandle(&val);
            unless(newDict, _heap.grow(dict, grow(dict.capacity()))) {return false;}
            if (!newDict.insert(key, val))
                return false;
            _stack.back() = newDict;
        }
        return true;
    }

    Heap& _heap;
    Handle<Value> _root;
    deque<Handle<Object>> _stack;
    deque<Handle<Symbol>> _keys;
    Handle<Maybe<snej::smol::String>> _emptyString;
    Handle<Maybe<Array>> _emptyArray;
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
        case Type::BigInt:  return writer.Int64(val.as<BigInt>().asInt());
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
        case Type::Vector: {
            if (!writer.StartArray()) return false;
            for (Value item : val.as<Vector>().items())
                if (!item || !writeVal(item, writer)) return false;
            return writer.EndArray();
        }
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
