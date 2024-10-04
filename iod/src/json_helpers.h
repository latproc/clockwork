#include <cJSON.h>
#include <set>
#include <map>
#include <string>
#include "value.h"

void copyJSONArrayToSet(cJSON *obj, const char *key, std::set<std::string> &res);
void copyJSONArrayToMap(cJSON *obj, const char *key, std::map<std::string, Value> &res,
                               const char *key_name = "property", const char *value_name = "type");
cJSON *StringSetToJSONArray(std::set<std::string> &items);
cJSON *MapToJSONArray(std::map<std::string, Value> &items, const char *key_name = "property",
                             const char *value_name = "type");
