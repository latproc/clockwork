#include "gtest/gtest.h"
#include <symboltable.h>
#include <value.h>
#include <cJSON.h>
#include <boost/context/fiber.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <json_expr_parser.h>
#include <json_expression.h>
#include <vector>
#include <deque>

namespace {

struct parser_exception : public std::runtime_error {
    boost::context::fiber f;
    parser_exception(boost::context::fiber&& f_,std::string const& what) :
        std::runtime_error{what},
        f{ std::move(f_) } { }
};

std::string parse(const std::string &str) {
    namespace ctx = boost::context;

    std::istringstream is(str);
    char token;
    Parser::TokenType kind = Parser::TokenType::expr;
    bool done = false;

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
    std::string result;
    while (!done) {
        result += token;
        try {
            source = std::move(source).resume(); // resume the parser
        }
        catch (const std::runtime_error &ex) {
            std::cerr << ex.what() << "\n";
            done = true;
			throw parser_exception(std::move(source), ex.what());
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

TEST(Parser, CanParseComplexExprWithSpaces) {
    EXPECT_EQ(parse("$.a [1] [0] .b .c [2]"), "$.a[1][0].b.c[2]");
}

TEST(Parser, CanUseStringIdentifiers) {
    EXPECT_EQ((parse("$.name")), "$.name");
}

TEST(Value, AssignJSON) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    Value val = cJSON_Parse(json_str);
    EXPECT_EQ(val.kind, Value::t_json) << "A JSON object is an object";
}

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
