#include <string>
#include <sstream>
#include <iomanip>
#include "cJSON.h"
#include <tl/expected.hpp>

std::string url_encode(const std::string &str);
tl::expected<std::string, std::string> url_encode_json(cJSON *json);
