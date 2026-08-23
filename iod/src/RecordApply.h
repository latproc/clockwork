#pragma once

#include <string>

struct cJSON;
class MachineClass;
class MachineInstance;
class Value;

namespace RecordApply {

std::string lowercase(const std::string &s);
std::string tableName(const MachineClass *mc);
bool typeMatches(const MachineClass *mc, const std::string &type);
Value jsonToValue(cJSON *item);
bool keyMatches(MachineInstance *m, const MachineClass *mc, cJSON *keys);

// Apply one JSON row onto every held RECORD instance of `type` whose KEY
// matches `keys` (or the KEY field in `row`). Creates Class#key if none exist.
// Does not persist. Returns the number of instances written.
int applyRow(const std::string &type, cJSON *keys, cJSON *row);

} // namespace RecordApply
