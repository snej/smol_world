//
// JSON.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "smol_world.hh"
#include <string>

namespace snej::smol {

Value newFromJSON(std::string_view json, Heap&, std::string* outError = nullptr);

Value newFromJSON(std::string const& json, Heap&, std::string* outError = nullptr);

std::string toJSON(Value);

}
