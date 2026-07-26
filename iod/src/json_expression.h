#pragma once

#include "cJSON.h"
#include <iostream>
#include <memory>
#include <string>
#include <value.h>
#include <boost/optional.hpp>

class SymbolTable;
class MachineInstance;

struct JsonDeleter {
    void operator()(cJSON *json) const {
        if (json) {
            cJSON_Delete(json);
        }
    }
};

using OwnedJson = std::unique_ptr<cJSON, JsonDeleter>;

inline OwnedJson own_json(cJSON *json) {
    return OwnedJson(json);
}

cJSON *apply(const std::string &str, cJSON *json);
cJSON *apply(const std::string &str, cJSON *json, SymbolTable *symbols);
cJSON *apply(const std::string &str, cJSON *json, MachineInstance* context);
cJSON *assign(const std::string &str, cJSON *json, const std::string &value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);
cJSON *assign(const std::string &str, cJSON *json, int value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

cJSON *assign(const std::string &str, cJSON *json, uint64_t value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

// Consumes `value` (pass `std::move(...)`); caller must not use moved-from payload.
// Ownership is released to the target JSON tree on success, otherwise deleted on return.
cJSON *assign_take(const std::string &str, cJSON *json, OwnedJson value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

// Clones `value`; caller retains ownership of the input.
cJSON *assign_clone(const std::string &str, cJSON *json, const cJSON *value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

// Assigns from a Value using copy semantics; JSON payloads are cloned.
cJSON *assign(const std::string &str, cJSON *json, const Value &value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

cJSON *assign(const std::string &str, cJSON *json, double value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);

cJSON *assign(const std::string &str, cJSON *json, bool value,
                boost::optional<SymbolTable *> symbols = boost::none,
                boost::optional<MachineInstance *> context = boost::none);


class JsonExpr {
  public:
    JsonExpr(const char *str) : expr(str) {}
    JsonExpr(const std::string &str) : expr{str} {}
    cJSON *apply(cJSON *json) const { return ::apply(expr, json); }

  private:
    std::string expr;
};
