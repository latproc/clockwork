#pragma once

#include <cJSON.h>
#include <iostream>
#include <string>
#include <value.h>

cJSON *apply(const std::string &str, cJSON *json);
cJSON *assign(const std::string &str, cJSON *json, const std::string &value);
cJSON *assign(const std::string &str, cJSON *json, int value);
cJSON *assign(const std::string &str, cJSON *json, uint64_t value);
cJSON *assign(const std::string &str, cJSON *json, const Value &value);
cJSON *assign(const std::string &str, cJSON *json, double value);
cJSON *assign(const std::string &str, cJSON *json, cJSON *value);
cJSON *assign(const std::string &str, cJSON *json, bool value);

class JsonExpr {
  public:
    JsonExpr(const char *str) : expr(str) {}
    JsonExpr(const std::string &str) : expr{str} {}
    cJSON *apply(cJSON *json) const { return ::apply(expr, json); }

  private:
    std::string expr;
};
