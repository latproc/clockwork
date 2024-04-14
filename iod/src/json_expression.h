#pragma once

#include <cJSON.h>
#include <string>
#include <iostream>

cJSON *apply(const std::string &str, cJSON *json);

class JsonExpr {
public:
    JsonExpr(const char *str) : expr(str) {}
    JsonExpr(const std::string & str) : expr{str} {}
    cJSON *apply(cJSON *json) const {
        return ::apply(expr, json);
    }
private:
    std::string expr;
};
