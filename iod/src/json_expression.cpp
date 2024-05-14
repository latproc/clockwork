#include <cJSON.h>
#include <boost/context/fiber.hpp>
#include <string>
#include "json_expr_parser.h"
#include <iostream>

cJSON *apply(const std::string &str, cJSON *json) {
    namespace ctx = boost::context;

    std::istringstream is(str);
    std::string token;
    Parser::TokenType kind = Parser::TokenType::expr;
    bool done = false;
    // execute parser in new fiber and process tokens in the main function
    ctx::fiber source{[&is, &token, &kind, &done](ctx::fiber &&sink) {
        Parser p(is,
            [&sink, &token, &kind](char token_, Parser::TokenType token_type) {
                token = token_;
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
    cJSON *tmp = json;
    while (!done) {
        switch (kind) {
            case Parser::TokenType::root:
                break;
            case Parser::TokenType::introducer:
                break;
            case Parser::TokenType::var:
                if (tmp && tmp->type == cJSON_Object) {
                    tmp = cJSON_GetObjectItem(tmp, token.c_str());
                }
                else {
                    throw std::runtime_error("not an object");
                }
                break;
            case Parser::TokenType::subs_begin:
                break;
            case Parser::TokenType::key:
                if (tmp && tmp->type == cJSON_Object) {
                    tmp = cJSON_GetObjectItem(tmp, token.c_str());
                }
                else {
                    throw std::runtime_error("not an object");
                }
                break;
            case Parser::TokenType::subs_end:
                break;
            case Parser::TokenType::index:
                if (tmp && tmp->type == cJSON_Array) {
                    tmp = cJSON_GetArrayItem(tmp, std::stoi(token));
                }
                else {
                    throw std::runtime_error("not an array");
                }
                break;
            case Parser::TokenType::wildcard:
                if (tmp && tmp->type == cJSON_Array) {
                    tmp = cJSON_GetArrayItem(tmp, 0);
                }
                else {
                    throw std::runtime_error("not an array");
                }
                break;
            default:
                throw std::runtime_error("unexpected token");
        }
        source = std::move(source).resume(); // resume the parser
    }

    cJSON *result = nullptr;
    if (tmp) {
        auto str = cJSON_Print(tmp);
        result = cJSON_Parse(str);
        free(str);
    }
    else {
        std::cerr << "Expression did not match\n";
    }
    return result;
}

