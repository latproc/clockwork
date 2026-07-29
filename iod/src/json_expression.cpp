#include "json_expr_parser.h"
#include <boost/optional.hpp>
#include "cJSON.h"
#include <iostream>
#include <sstream>
#include <json_expression.h>
#include <string>
#include <vector>
#include <symboltable.h>
#include <MachineInstance.h>
#include <MessageLog.h>
#include <value.h>

namespace {

struct PathResult {
    cJSON *parent;
    cJSON *value;
    boost::optional<std::string> key;
    boost::optional<int> index;
    explicit PathResult(cJSON *value_ = nullptr) : parent(nullptr), value(value_) {}
};

// Token event from the JSON path parser. Collect all tokens first, then walk
// the path — avoids boost::context::fiber (not available on Ubuntu Bionic's
// Boost 1.65 packages). Processing does not feed back into the parser, so
// batching is equivalent to the old coroutine interleaving.
struct PathToken {
    Parser::TokenType kind;
    std::string token;
    size_t index;
};

PathResult follow_json_expr_path(const std::string &str,
                cJSON *json,
                boost::optional<SymbolTable*> symbols = boost::none,
                boost::optional<MachineInstance*> context = boost::none) {
    Parser::StringInputStream is(str);
    std::vector<PathToken> tokens;
    Parser p(
        is,
        [&tokens](char token_, Parser::TokenType token_type) {
            tokens.push_back(PathToken{token_type, std::string(1, token_), 0});
        },
        [&tokens](size_t index_, Parser::TokenType token_type) {
            tokens.push_back(PathToken{token_type, std::string(), index_});
        },
        [&tokens](std::string token_, Parser::TokenType token_type) {
            tokens.push_back(PathToken{token_type, std::move(token_), 0});
        });
    p.run();

    PathResult result(json);
    for (const PathToken &ev : tokens) {
        const Parser::TokenType kind = ev.kind;
        const std::string &token = ev.token;
        const size_t index = ev.index;
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
                result.index = static_cast<int>(index);
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
                    result.index = static_cast<int>(index);
                }
                else {
                    std::stringstream ss;
                    ss << "symbol not an integer: " << token;
                    throw std::runtime_error(ss.str());
                }
                result.value = cJSON_GetArrayItem(result.value, *result.index);
            }
            else if (result.value && result.value->type == cJSON_Object) {
                if (symbol.kind != Value::t_symbol && symbol.kind != Value::t_string && symbol.kind != Value::t_integer) {
                    std::cout << "expected a symbol or string but got: " << symbol.kind_to_string() << std::endl;
                    throw std::runtime_error("symbol not a string");
                }
                result.parent = result.value;
                result.key = symbol.kind == Value::t_integer ? symbol.asString() : symbol.sValue;
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
    }
    return result;
}

void show_json_error(const std::string & err, const std::string &str, cJSON *json, MachineInstance *context = nullptr) {
    char *json_str = nullptr;
    if (json) {
        json_str = cJSON_PrintUnformatted(json);
    }
    MessageLog::instance()->get_stream() << (context ? context->fullName() : "") << " " << err
    << " when assigning " << str << " in "
    << short_form_value(json_str ? json_str : "<null>") << std::endl;
    MessageLog::instance()->release_stream();
    if (json_str) {
        free(json_str);
    }
}

} // namespace

// apply() must return a newly owned tree (or nullptr). Callers wrap the
// result in Value(cJSON*) / free it. Prefer clone_json (cJSON_Duplicate)
// over Print+Parse: same ownership, less CPU and allocator churn under
// heavy ITEM ... OF json traffic.
cJSON *apply(const std::string &str, cJSON *json) {
    try {
        cJSON *result = follow_json_expr_path(str, json, boost::none).value;
        return result ? clone_json(result) : nullptr;
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json);
        return nullptr;
    }
}

cJSON *apply(const std::string &str, cJSON *json, SymbolTable* symbols) {
    try {
        cJSON *result = follow_json_expr_path(str, json, symbols).value;
        return result ? clone_json(result) : nullptr;
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json);
        return nullptr;
    }
}

cJSON *apply(const std::string &str, cJSON *json, MachineInstance* context) {
    try {
        cJSON *result = follow_json_expr_path(str, json, boost::none, context).value;
        return result ? clone_json(result) : nullptr;
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json, context);
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
        show_json_error(err.what(), str, json, context ? *context : nullptr);
        result.parent = nullptr;
        result.value = nullptr;
    }
    auto item = cJSON_CreateString(value.c_str());
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), item);
                return json;
            }
            if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, item);
                return json;
            }
        }
        else {
            cJSON_Delete(json);
            json = item;
            return json;
        }
    }
    if (json->type == cJSON_Object) {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, result.key ? result.key->c_str() : str.c_str(), item);
        return json;
    }
    show_json_error("could not resolve key/index: ",str, json, context ? *context : nullptr);
    cJSON_Delete(item);
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, uint64_t value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!json) { return json; }
    PathResult result;
    try {
        result = follow_json_expr_path(str, json, symbols, context);
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json, context ? *context : nullptr);
        result.parent = nullptr;
        result.value = nullptr;
    }
    auto item = cJSON_CreateNumber(value);
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), item);
                return json;
            }
            if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, item);
                return json;
            }
        }
        else {
            cJSON_Delete(json); // FIXME: not a reference paraemter
            json = item;
            return json;
        }
    }
    if (json->type == cJSON_Object) {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, result.key ? result.key->c_str() : str.c_str(), item);
        return json;
    }
    show_json_error("could not resolve key/index: ",str, json, context ? *context : nullptr);
    cJSON_Delete(item);
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, bool value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!json) { return json; }
    PathResult result;
    try {
        result = follow_json_expr_path(str, json, symbols, context);
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json, context ? *context : nullptr);
        result.parent = nullptr;
        result.value = nullptr;
    }
    auto item = value ? cJSON_CreateTrue() : cJSON_CreateFalse();
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), item);
                return json;
            }
            if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, item);
                return json;
            }
        }
        else {
            cJSON_Delete(json); // FIXME: not a reference parameter
            json = item;
            return json;
        }
    }
    if (json->type == cJSON_Object) {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, result.key ? result.key->c_str() : str.c_str(), item);
        return json;
    }
    cJSON_Delete(item);
    return json;
}

cJSON *assign(const std::string &str, cJSON *json, double value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!json) { return json; }
    PathResult result;
    try {
        result = follow_json_expr_path(str, json, symbols, context);
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json, context ? *context : nullptr);
        result.parent = nullptr;
        result.value = nullptr;
    }
    auto item = cJSON_CreateDouble(value);
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), item);
                return json;
            }
            if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, item);
                return json;
            }
        }
        else {
            cJSON_Delete(json);
            json = item;
            return json;
        }
    }
    if (json->type == cJSON_Object) {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, result.key ? result.key->c_str() : str.c_str(), item);
        return json;
    }
    cJSON_Delete(item);
    return json;
}

cJSON *assign_take(const std::string &str, cJSON *json, OwnedJson value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!value) {
        return json;
    }
    if (!json) {
        return json;
    }
    PathResult result;
    bool path_error = false;
    try {
        result = follow_json_expr_path(str, json, symbols, context);
    }
    catch (const std::runtime_error &err) {
        show_json_error(err.what(), str, json, context ? *context : nullptr);
        result.parent = nullptr;
        result.value = nullptr;
        path_error = true;
    }
    if (path_error) {
        return json;
    }
    if (result.value) {
        if (result.parent) {
            if (result.key) {
                cJSON_ReplaceItemInObject(result.parent, result.key->c_str(), value.release());
                return json;
            }
            if (result.index) {
                cJSON_ReplaceItemInArray(result.parent, *result.index, value.release());
                return json;
            }
        }
        else {
            cJSON_Delete(json);
            return value.release();
        }
    }
    if (json->type == cJSON_Object) {
        // add a new key to the parent object
        cJSON_AddItemToObject(json, result.key ? result.key->c_str() : str.c_str(), value.release());
        return json;
    }
    show_json_error("could not resolve key/index: ",str, json, context ? *context : nullptr);
    return json;
}

cJSON *assign_clone(const std::string &str, cJSON *json, const cJSON *value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    if (!value) {
        return json;
    }
    return assign_take(str, json, own_json(clone_json(value)), symbols, context);
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
        return assign_take(str, json, own_json(value.bValue ? cJSON_CreateTrue() : cJSON_CreateFalse()), symbols, context);
    case Value::t_empty:
        return assign_take(str, json, own_json(cJSON_CreateNull()), symbols, context);
    case Value::t_json:
        return assign_clone(str, json, value.json, symbols, context);
    default:
        throw std::runtime_error("unsupported value type for assignment");
    }
}

cJSON *assign(const std::string &str, cJSON *json, int value,
                boost::optional<SymbolTable *> symbols,
                boost::optional<MachineInstance *> context) {
    return assign(str, json, static_cast<uint64_t>(value), symbols, context);
}
