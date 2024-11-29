#pragma once

#include "Parameter.h"
#include <vector>

typedef std::vector<Parameter> ParameterArray;

// Convert a ParameterArray to a string with a limited length.
std::string toLimitedString(const ParameterArray &array, size_t limit = 600);

// Convert a ParameterArray to a string.
std::string toString(const ParameterArray &array);
