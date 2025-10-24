#include "json_expr_parser.h"
#include <boost/context/fiber.hpp>
#include <boost/optional.hpp>
#include <cJSON.h>
#include <iostream>
#include <json_expression.h>
#include <string>
#include <symboltable.h>
#include <MachineInstance.h>

namespace {

struct PathResult {
    cJSON *parent;
    cJSON *value;
    boost::optional<std::string> key;
    boost::optional<int> index;
    explicit PathResult(cJSON *value_ = nullptr) : parent(nullptr), value(value_) {}
};

PathResult follow_json_expr_path(const std::string &str,
                cJSON *json,
                boost::optional<SymbolTable*> symbols = boost::none,
                boost::optional<MachineInstance*> context = boost::none) {
    namespace ctx = boost::context;

    Parser::StringInputStream is(str);
    std::string token;
    size_t index = 0;
    Parser::TokenType kind = Parser::TokenType::expr;
    bool done = false;
    // execute parser in new fiber and process tokens in the main function
    ctx::fiber source{[&is, &token, &index, &kind, &done](ctx::fiber &&sink) {
        Parser p(
            is,
            [&sink, &token, &kind](char token_, Parser::TokenType token_type) {
                token = token_;
                kind = token_type;
                sink = std::move(sink).resume();
            },
            [&sink, &index, &kind](size_t index_, Parser::TokenType token_type) {
                index = index_;
                kind = token_type;
                sink = std::move(sink).resume();
            },
            [&sink, &token, &kind](std::string token_, Parser::TokenType token_type) {
                token = token_;
                kind = token_type;
                sink = std::move(sink).resume();
            });
        p.run();
        done = true;
        return std::move(sink); // resume the main fiber
    }};

    source = std::move(source).resume();
    PathResult result(json);

    while (!done) {
        switch (kind) {
        case Parser::TokenType::root:
            break;
        case Parser::TokenType::introducer:
            break;
        case Parser::TokenType::symbol_begin:
            break;
        case Parser::TokenType::var:
            if (result.value && result.value->type == cJSON_Object) {
                result.parent = result.value;
                result.key = token;
                result.index.reset();
                result.value = cJSON_GetObjectItem(result.value, token.c_str());
            }
            else {
                throw std::runtime_error("not an object");
            }
            break;
        case Parser::TokenType::subs_begin:
            break;
        case Parser::TokenType::key:
            if (result.value && result.value->type == cJSON_Object) {
                result.parent = result.value;
                result.key = token;
                result.index.reset();
                result.value = cJSON_GetObjectItem(result.value, token.c_str());
            }
            else {
                throw std::runtime_error("not an object");
            }
            break;
        case Parser::TokenType::subs_end:
            break;
        case Parser::TokenType::index:
            if (result.value && result.value->type == cJSON_Array) {
                result.parent = result.value;
                result.key.reset();
                result.index = index;
                result.value = cJSON_GetArrayItem(result.parent, *result.index);
            }
            else {
                throw std::runtime_error("not an array");
            }
            break;
        case Parser::TokenType::wildcard:
            if (result.value && result.value->type == cJSON_Array) {
                result.parent = result.value;
                result.key.reset();
                result.index = 0;
                result.value = cJSON_GetArrayItem(result.value, 0);
            }
            else {
                throw std::runtime_error("not an array");
            }
            break;
        case Parser::TokenType::symbol:
        {
            Value symbol;
            if (symbols) {
                symbol = (*symbols)->lookup(token.c_str());
                if (symbol == SymbolTable::Null) {
                    std::stringstream ss;
                    ss << "symbol not found: " << token;
                    throw std::runtime_error(ss.str());
                }
            }
            else if (context) {
                symbol = (*context)->getValue(token);
                if (symbol == SymbolTable::Null) {
                    std::stringstream ss;
                    ss << "symbol not found: " << token;
                    throw std::runtime_error(ss.str());
                }
            }
            else {
                throw std::runtime_error("no symbol table");
            }
            if (result.value && result.value->type == cJSON_Array) {
                result.parent = result.value;
                result.key.reset();
                int64_t index = 8;
                if (symbol.asInteger(index)) {
                    result.index = index;
                }
                else {
                    std::stringstream ss;
                    ss << "symbol not an integer: " << token;
                    throw std::runtime_error(ss.str());
                }
                result.value = cJSON_GetArrayItem(result.value, *result.index);
            }
            else if (result.value && result.value->type == cJSON_Object) {
                if (symbol.kind != Value::t_symbol && symbol.kind != Value::t_string) {
                    throw std::runtime_error("symbol not a string");
                }
                result.parent = result.value;
                result.key = symbol.sValue;
                result.index.reset();
                result.value = cJSON_GetObjectItem(result.value, result.key->c_str());
            }
            else {
                std::stringstream ss;
                ss << "not an array or object: " << token;
                throw std::runtime_error(ss.str());
            }
            break;
        }
        default: {
            std::stringstream ss;
            ss << "unexpected token " << token;
            throw std::runtime_error(ss.str());
            }
        }
        source = std::move(source).resume(); // resume the parser
    }
    return result;
}

} // namespace

cJSON *apply(const std::string &str, cJSON *json) {
    try {
        cJSON *result = follow_json_expr_path(str, json, boost::none).value;
        if (result) {
            auto str = cJSON_Print(result);
            result = cJSON_Parse(str);
            free(str);
        }
        return result;
    }
    catch (const std::runtime_error &err) {
        return nullptr;
    }
}

cJSON *apply(const std::string &str, cJSON *json, SymbolTable* symbols) {
    try {
        cJSON *result = follow_json_expr_path(str, json, symbols).value;
        if (result) {
            auto str = cJSON_Print(result);
            result = cJSON_Parse(str);
            free(str);
        }
        return result;
    }
    catch (const std::runtime_error &err) {
        return nullptr;
    }
}

cJSON *apply(const std::string &str, cJSON *json, MachineInstance* context) {
    try {
        cJSON *result = follow_json_expr_path(str, json, boost::none, context).value;
        if (result) {
            auto str = cJSON_Print(result);
            result = cJSON_Parse(str);
            free(str);
        }
        return result;
    }
    catch (const std::runtime_error &err) {
        return nullptr;
    }
}

cJSON *assign(const std::string &str, cJSON *json, const std::string &value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!json) { return json; }
    PathResult result;
    try {
         result = follow_json_expr_path(str, json, symbols, context);
    }
    catch (const std::runtime_error &err) {
        result.parent = nullptr;
        result.value = nullptr;
    }
    auto string = cJSON_CreateString(value.c_str());
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), string);
            }
            else if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, string);
            }
        }
        else {
            json = string;
        }
    }
    else {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, str.c_str(), string);
    }
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, uint64_t value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    auto result = follow_json_expr_path(str, json, symbols, context);
    auto number = cJSON_CreateNumber(value);
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), number);
            }
            else if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, number);
            }
        }
        else {
            json = number;
        }
    }
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, bool value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    auto result = follow_json_expr_path(str, json, symbols, context);
    auto boolean = value ? cJSON_CreateTrue() : cJSON_CreateFalse();
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), boolean);
            }
            else if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, boolean);
            }
        }
        else {
            json = boolean;
        }
    }
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, double value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    auto result = follow_json_expr_path(str, json, symbols, context);
    auto number = cJSON_CreateDouble(value);
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), number);
            }
            else if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, number);
            }
        }
        else {
            if (result.value) { cJSON_Delete(result.value); }
            result.value = number;
        }
    }
    else {
        result.value = number;
    }
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, cJSON *value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    auto result = follow_json_expr_path(str, json, symbols, context);
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), value);
            }
            else if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, value);
            }
        }
        else {
            if (result.value) { cJSON_Delete(result.value); }
            result.value = value;
        }
    }
    else {
        result.value = value;
    }
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, const Value &value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    switch (value.kind) {
    case Value::t_string:
    case Value::t_symbol:
        return assign(str, json, value.sValue, symbols, context);
    case Value::t_integer:
        return assign(str, json, (uint64_t)value.iValue, symbols, context);
    case Value::t_float:
        return assign(str, json, value.fValue, symbols, context);
    case Value::t_bool:
        return assign(str, json, value.bValue ? cJSON_CreateTrue() : cJSON_CreateFalse(), symbols, context);
    case Value::t_empty:
        return assign(str, json, cJSON_CreateNull(), symbols, context);
    case Value::t_json:
        return assign(str, json, value.json, symbols, context);
    default:
        throw std::runtime_error("unsupported value type for assignment");
    }
}

cJSON *assign(const std::string &str, cJSON *json, int value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    return assign(str, json, (uint64_t)value, symbols, context);
}
