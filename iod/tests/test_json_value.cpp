#include "gtest/gtest.h"
#include <symboltable.h>
#include <value.h>
#include <cJSON.h>
#include <boost/context/fiber.hpp>
#include <iostream>
#include <sstream>
#include <string>

namespace {


// A JSON expression is an expression that can be applied to a JSON
// document to extract a value or a subdocument.
//
// Boost.Context reference: https://www.boost.org/doc/libs/1_84_0/libs/context/doc/html/context/ff.html

// This implemetation only supports single character member names and
// single digit array indices.
class Parser {
 
    // Grammar: { } => 0 or more, ( ) => group, | => or
    // expr = root | root member 
    // member = { ( "[" key "]" | "." var ) }
    // root = "$"
    // var  = alpha
    // key = "*" | var || number

    char pull() { return std::char_traits<char>::to_char_type(is.get()); }

    void scan() {
        do {
            next = pull();
        } while (isspace(next));
    }

  public:
    enum class TokenType { expr, root, introducer, member, var, subs_begin, key, subs_end, index, wildcard};

    Parser(std::istream &is_, std::function<void(char, TokenType kind)> cb_) : next(), is(is_), cb(cb_) {}

    void run() {
        scan();
        root();
        if (!is.eof()) {
            member();
        }
    }

    static std::ostream & display(std::ostream & out, TokenType kind) {
         switch(kind) {
             case TokenType::expr: out << "expr"; break;
             case TokenType::root: out << "root"; break;
             case TokenType::introducer: out << "introducer"; break;
             case TokenType::member: out << "member"; break;
             case TokenType::var: out << "var"; break;
             case TokenType::subs_begin: out << "subs_begin"; break;
             case TokenType::key: out << "key"; break;
             case TokenType::subs_end: out << "subs_end"; break;
             case TokenType::index: out << "index"; break;
             case TokenType::wildcard: out << "wildcard"; break;
         };
         return out;
    }

  private:
    char next;
    std::istream &is;
    std::function<void(char, TokenType)> cb;

    void root() {
        if (next == '$') {
            cb(next, TokenType::root);
            scan();
        }
        else {
            throw std::runtime_error("no root node");
        }
    }

    void member() {
        while (!is.eof()) {
            if (next == '[') {
                cb(next, TokenType::subs_begin);
                scan();
                key();
                if (next != ']') {
                    throw std::runtime_error("parse error");
                }
                cb(next, TokenType::subs_end);
                scan();
            }
            else {
                while (next == '.') {
                    cb(next, TokenType::introducer);
                    scan();
                    var();
                }
            }
        }
    }

    void var() {
        if (isalpha(next)) {
            cb(next, TokenType::var);
            scan();
        }
        else {
            throw std::runtime_error("parse error");
        }
    }

    void key() {
        if (isdigit(next)) {
            cb(next, TokenType::index);
            scan();
        }
        else if (isalpha(next)) {
            cb(next, TokenType::key);
            scan();
        }
        else if (next == '*') {
            cb(next, TokenType::wildcard);
            scan();
        }
        else {
            throw std::runtime_error("parsing failed");
        }
    }
};

std::ostream & operator<<(std::ostream & out, Parser::TokenType kind) {
    return Parser::display(out, kind);
}

std::string parse(const std::string &str) {
    namespace ctx = boost::context;

    std::istringstream is(str);
    char token;
    Parser::TokenType kind = Parser::TokenType::expr;
    bool done = false;
    // execute parser in new fiber and process tokens in the main function
    ctx::fiber source{[&is, &token, &kind, &done](ctx::fiber &&sink) {
        // create parser with callback function
        Parser p(is, [&sink, &token, &kind](char token_, Parser::TokenType token_type) {
            token = token_;
            kind = token_type;
            sink = std::move(sink).resume();
        });
        p.run();
        done = true;
        return std::move(sink); // resume the main fiber
    }};
    source = std::move(source).resume();
    std::string result;
    while (!done) {
        std::cerr << "parsed: " << kind << ": " << token << "\n";
        result += token;
        try {
            source = std::move(source).resume(); // resume the parser
        }
        catch (const std::runtime_error &ex) {
            std::cerr << ex.what() << "\n";
            done = true;
        }
    }
    return result;
}

TEST(Parser, CanParseSimpleExpr) {
    EXPECT_EQ(parse("$"), "$");
}

TEST(Parser, CanParseMember) {
    EXPECT_EQ(parse("$.a"), "$.a");
}

TEST(Parser, CanParseArray) {
    EXPECT_EQ(parse("$.a[1]"), "$.a[1]");
}

TEST(Parser, CanParseComplexExpr) {
    EXPECT_EQ(parse("$.a[1][0].b.c[2]"), "$.a[1][0].b.c[2]");
}

TEST(Value, AssignJSON) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    Value val = cJSON_Parse(json_str);
    EXPECT_EQ(val.kind, Value::t_json) << "A JSON object is an object";
}

cJSON *apply(const std::string &str, cJSON *json) {
    namespace ctx = boost::context;

    std::istringstream is(str);
    std::string token;
    Parser::TokenType kind = Parser::TokenType::expr;
    bool done = false;
    // execute parser in new fiber and process tokens in the main function
    ctx::fiber source{[&is, &token, &kind, &done](ctx::fiber &&sink) {
        Parser p(is, [&sink, &token, &kind](char token_, Parser::TokenType token_type) {
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
        std::cerr << "parsed: " << kind << ": " << token << "\n";
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
        std::cerr << "result: " << str << "\n";
        result = cJSON_Parse(str);
        free(str);
    }
    else {
        std::cerr << "Expression did not match\n";
    }
    return result;
}

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

bool equal(cJSON *a, cJSON *b) {
    auto a_str = cJSON_Print(a);
    auto b_str = cJSON_Print(b);
    bool result = strcmp(a_str, b_str) == 0;
    free(a_str);
    free(b_str);
    return result;
}

TEST(JsonExpr, SelectEntireExpression) {
    auto expr_str = "$";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_TRUE(equal(json, doc));
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(JsonExpr, SelectObjectMember) {
    auto expr_str = "$.b";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(JsonExpr, SelectInvalidObjectMember) {
    auto expr_str = "$.c";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_EQ(json, nullptr);
    cJSON_Delete(doc);
}

TEST(JsonExpr, SelectArrayElement) {
    auto expr_str = "$[1]";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON([1, "hello"])JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(JsonExpr, SelectInvalidArrayElement) {
    auto expr_str = "$[2]";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON([1, "hello"])JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_EQ(json, nullptr);
    cJSON_Delete(doc);
}

} // namespace
