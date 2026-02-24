#include "gtest/gtest.h"
#include <boost/context/fiber.hpp>
#include "cJSON.h"
#include <deque>
#include <iostream>
#include <json_expr_parser.h>
#include <json_expression.h>
#include <sstream>
#include <string>
#include <symboltable.h>
#include <value.h>
#include <vector>
#include <urlencode.h>

#include "library_globals.cpp"

namespace {

struct parser_exception : public std::runtime_error {
    boost::context::fiber f;
    parser_exception(boost::context::fiber &&f_, std::string const &what)
        : std::runtime_error{what}, f{std::move(f_)} {
        std::cerr << "parser_exception: " << what << "\n";
    }
};

TEST(StringInputStream, CanReadString) {
    Parser::StringInputStream is{"hi"};
    EXPECT_EQ(is.get(), 'h');
    EXPECT_EQ(is.get(), 'i');
    EXPECT_FALSE(is.eof());
    EXPECT_EQ(is.get(), (char)0xff);
    EXPECT_TRUE(is.eof());
}

TEST(CStringInputStream, CanReadString) {
    Parser::CStringInputStream is{"hi"};
    EXPECT_EQ(is.get(), 'h');
    EXPECT_EQ(is.get(), 'i');
    EXPECT_FALSE(is.eof());
    EXPECT_EQ(is.get(), (char)0xff);
    EXPECT_TRUE(is.eof());
}

std::string parse(Parser::InputStream &is) {
    namespace ctx = boost::context;

    Parser::TokenType kind = Parser::TokenType::expr;
    std::string token;
    size_t index = 0;
    bool done = false;

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
            [&sink, &token, &kind](std::string value, Parser::TokenType token_type) {
                token = value;
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
        if (kind != Parser::TokenType::index) {
            result += token;
        }
        else {
            result += std::to_string(index);
        }
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

std::string parse(const std::string &str) {
    Parser::CStringInputStream is{str.c_str()};
    return parse(is);
}

TEST(Parser, CanParseSimpleExpr) { EXPECT_EQ(parse("$"), "$"); }

TEST(Parser, CanParseMember) { EXPECT_EQ(parse("$.a"), "$.a"); }

TEST(Parser, CanParseMemberWithDefaultRoot) { EXPECT_EQ(parse("a"), "a"); }

TEST(Parser, CanParseArrayIndex) { EXPECT_EQ(parse("$.a[1]"), "$.a[1]"); }

TEST(Parser, CanParseArrayIndexWithDefaultRoot) { EXPECT_EQ(parse("[1]"), "[1]"); }

TEST(Parser, CanParseTwoDigitArrayIndex) { EXPECT_EQ(parse("$.a[10]"), "$.a[10]"); }

TEST(Parser, CanParseComplexExpr) { EXPECT_EQ(parse("$.a[1][0].b.c[2]"), "$.a[1][0].b.c[2]"); }

TEST(Parser, CanParseComplexExprWithSpaces) {
    EXPECT_EQ(parse("$.a [1] [0] .b .c [2]"), "$.a[1][0].b.c[2]");
}

TEST(Parser, CanUseStringIdentifiers) { EXPECT_EQ((parse("$.name")), "$.name"); }

TEST(Parser, CanUseStringKey) { EXPECT_EQ((parse("$.a[key]")), "$.a[key]"); }

TEST(Parser, CanUseWildcard) { EXPECT_EQ((parse("$.a[*]")), "$.a[*]"); }

TEST(Parser, ParsingStringStopsAtInvalidCharacter) {
    Parser::StringInputStream is{"$.a[1]x"};
    EXPECT_EQ(parse(is), "$.a[1]");
    EXPECT_EQ(is.get(), 'x');
    EXPECT_FALSE(is.eof());
    is.get();
    EXPECT_TRUE(is.eof());
}

TEST(Parser, ParsingCStringStopsAtInvalidCharacter) {
    Parser::CStringInputStream is{"$.a[1]x"};
    EXPECT_EQ(parse(is), "$.a[1]");
    EXPECT_EQ(is.get(), 'x');
    EXPECT_FALSE(is.eof());
    is.get();
    EXPECT_TRUE(is.eof());
}

TEST(Value, AssignJSON) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    Value val{cJSON_Parse(json_str)};
    EXPECT_EQ(val.kind, Value::t_json);
    Value val2{val};
    EXPECT_EQ(val2.kind, Value::t_json);
    std::string val_str = val.asString();
    std::string val2_str = val2.asString();
    EXPECT_EQ(val_str, val2_str);
    val = val; // self assignment
    EXPECT_EQ(val.kind, Value::t_json);
    std::string val_str2 = val.asString();
    EXPECT_EQ(val_str2, val_str) << "object changed by assignment to itself";
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
    EXPECT_EQ(strcmp(json->valuestring, "hello"), 0);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

    TEST(JsonExpr, SelectNestedObjectMember) {
    auto expr_str = "$.a.1.c";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON({"a":{"1":{"c":"hello"}}})JSON";
    auto doc = cJSON_Parse(json_str);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    EXPECT_EQ(strcmp(json->valuestring, "hello"), 0);
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

TEST(JsonExpr, SelectStringArrayElement) {
    auto expr_str = "$.b";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto doc = cJSON_Parse(json_str);
    Value val = get_value(expr.apply(doc));
    EXPECT_EQ(val.kind, Value::t_string);
    EXPECT_EQ(val, Value{"hello"});
    cJSON_Delete(doc);
}

TEST(JsonExpr, SelectNumberArrayElement) {
    auto expr_str = "$[0]";
    JsonExpr expr(expr_str);
    auto json_str = R"JSON([1, "hello"])JSON";
    auto doc = cJSON_Parse(json_str);
    Value val{get_value(expr.apply(doc))};
    EXPECT_EQ(val.kind, Value::t_integer);
    EXPECT_EQ(val, Value{1});
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignStringValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "$.b";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    assign(expr_str, doc, std::string("world"));
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    EXPECT_EQ(strcmp(json->valuestring, "world"), 0);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    EXPECT_EQ(strcmp(updated_json_str, R"JSON({"a":1,"b":"world"})JSON"), 0);
    free(updated_json_str);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignNewTopLevelValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "c";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    assign(expr_str, doc, std::string("world"));
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    EXPECT_EQ(strcmp(json->valuestring, "world"), 0);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    EXPECT_EQ(strcmp(updated_json_str, R"JSON({"a":1,"b":"hello","c":"world"})JSON"), 0);
    free(updated_json_str);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignNewNestedValue) {
    auto json_str = R"JSON({"a":1,"b":{"one":"hello"}})JSON";
    auto expr_str = "b.two";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    assign(expr_str, doc, std::string("world"));
    std::cout << "Assigning a new key into a nested object is unsupported\n";
    cJSON *json = expr.apply(doc);
    EXPECT_EQ(json, nullptr);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignIntegerNumberValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "$.a";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    assign(expr_str, doc, 2);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Number);
    EXPECT_EQ(json->valueint, 2);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    EXPECT_EQ(strcmp(updated_json_str, R"JSON({"a":2,"b":"hello"})JSON"), 0);
    free(updated_json_str);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignDoubleNumberValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "$.a";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    assign(expr_str, doc, 2.5);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Number);
    EXPECT_EQ(json->valuedouble, 2.5);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"a":2.5,"b":"hello"})JSON");
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignJsonValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "$.a";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    auto new_json = cJSON_Parse(R"JSON({"c":3})JSON");
    assign_take(expr_str, doc, new_json);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Object);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"a":{"c":3},"b":"hello"})JSON");
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignArrayValue) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "a";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    auto new_json = cJSON_Parse(R"JSON([1,2,3])JSON");
    assign_take(expr_str, doc, new_json);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Array);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"a":[1,2,3],"b":"hello"})JSON");
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignIntoArray) {
    auto json_str = R"JSON({"a":[1,2,3],"b":"hello"})JSON";
    auto expr_str = "$.a[1]";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    cJSON *tmp = assign(expr_str, doc, 4);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Number);
    EXPECT_EQ(json->valueint, 4);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"a":[1,4,3],"b":"hello"})JSON");
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignNewKeyInJsonObject) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto expr_str = "$.new";
    JsonExpr expr(expr_str);
    auto doc = cJSON_Parse(json_str);
    auto new_json = cJSON_Parse(R"JSON({"c":3})JSON");
    assign_take(expr_str, doc, new_json);
    cJSON *json = expr.apply(doc);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Object);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"a":1,"b":"hello","new":{"c":3}})JSON");
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(AssignJsonExpr, AssignToNullDocumentReturnsNull) {
    cJSON *doc = nullptr;
    EXPECT_EQ(nullptr, assign("$.a", doc, std::string("x")));
    EXPECT_EQ(nullptr, assign("$.a", doc, 1));
    EXPECT_EQ(nullptr, assign("$.a", doc, static_cast<uint64_t>(1)));
    EXPECT_EQ(nullptr, assign("$.a", doc, true));
    EXPECT_EQ(nullptr, assign("$.a", doc, 1.5));
    EXPECT_EQ(nullptr, assign_take("$.a", doc, cJSON_CreateObject()));
    EXPECT_EQ(nullptr, assign("$.a", doc, Value(cJSON_CreateObject())));
}


TEST(JsonExpr, CanSetExistinhValueInObjectUsingProperty) {
    auto json_str = R"JSON({"b":1})JSON";
    auto expr_str = "$.@property";
    JsonExpr expr(expr_str);
    SymbolTable symbols;
    symbols.add("property", Value{"b"});
    auto doc = cJSON_Parse(json_str);
    EXPECT_NE(nullptr, doc);
    cJSON *json = assign(expr_str, doc, 4, &symbols, nullptr);
    EXPECT_EQ(json, doc);
    EXPECT_EQ(doc->type, cJSON_Object);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"b":4})JSON");
    EXPECT_NE(updated_json_str, nullptr);
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(doc);
}

TEST(JsonExpr, CanSetNewValueInObjectUsingProperty) {
    auto json_str = R"JSON({"b":1})JSON";
    auto expr_str = "$.@property";
    JsonExpr expr(expr_str);
    SymbolTable symbols;
    symbols.add("property", Value{"a"});
    auto doc = cJSON_Parse(json_str);
    EXPECT_NE(nullptr, doc);
    cJSON *json = assign(expr_str, doc, 4, &symbols, nullptr);
    EXPECT_EQ(json, doc);
    EXPECT_EQ(doc->type, cJSON_Object);
    auto updated_json_str = cJSON_PrintUnformatted(doc);
    auto expected_json = cJSON_Parse(R"JSON({"b":1,"a":4})JSON");
    EXPECT_NE(updated_json_str, nullptr);
    auto expected_json_str = cJSON_PrintUnformatted(expected_json);
    EXPECT_EQ(strcmp(updated_json_str, expected_json_str), 0);
    free(updated_json_str);
    free(expected_json_str);
    cJSON_Delete(expected_json);
    cJSON_Delete(doc);
}


TEST(JsonExpr, CanGetValueFromArrayUsingIndexProperty) {
    auto json_str = R"JSON({"a":[1,2,3],"b":"hello"})JSON";
    auto expr_str = "$.a[@index]";
    SymbolTable symbols;
    symbols.add("index", Value{1});
    auto doc = cJSON_Parse(json_str);
    cJSON *json = apply(expr_str, doc, &symbols);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_Number);
    EXPECT_EQ(json->valueint, 2);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(JsonExpr, CanGetValueFromObjectUsingProperty) {
    auto json_str = R"JSON({"a":1, "b": "hello"})JSON";
    auto expr_str = "$.@property";
    SymbolTable symbols;
    symbols.add("property", Value{"b"});
    auto doc = cJSON_Parse(json_str);
    cJSON *json = apply(expr_str, doc, &symbols);
    EXPECT_NE(doc, nullptr);
    EXPECT_NE(json, nullptr);
    EXPECT_EQ(json->type, cJSON_String);
    EXPECT_EQ(strcmp(json->valuestring, "hello"), 0);
    cJSON_Delete(json);
    cJSON_Delete(doc);
}

TEST(UrlEncode, CanEncodeStringWithSpecialCharacters) {
    std::string str = "hello world!";
    std::string encoded = url_encode(str);
    EXPECT_EQ(encoded, "hello%20world%21");
}

TEST(UrlEncode, CanEncodeJSON) {
    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    auto doc = cJSON_Parse(json_str);
    auto encoded = url_encode_json(doc);
    EXPECT_TRUE(encoded);
    EXPECT_EQ(*encoded, "a=1&b=hello");
    cJSON_Delete(doc);
}

TEST(UrlEncode, CannotEncodeInvalidJSON) {
    auto json_str = R"JSON("a"})JSON";
    auto doc = cJSON_Parse(json_str);
    auto encoded = url_encode_json(doc);
    EXPECT_FALSE(encoded);
    cJSON_Delete(doc);
}

TEST(UrlEncode, CannotEncodeNestedJSON) {
    auto json_str = R"JSON({"a":1,"b":{"c":2}})JSON";
    auto doc = cJSON_Parse(json_str);
    auto encoded = url_encode_json(doc);
    EXPECT_FALSE(encoded);
    cJSON_Delete(doc);
}

} // namespace
