#include "gtest/gtest.h"

#include <cJSON.h>
#include <json_expression.h>
#include <value.h>

#include "library_globals.cpp"

namespace {

std::string jsonToString(cJSON *json) {
    if (!json) {
        return "";
    }
    char *str = cJSON_PrintUnformatted(json);
    std::string out = str ? str : "";
    free(str);
    return out;
}

TEST(JsonOwnership, AssignFromJsonValueClonesPayload) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);

    {
        Value rhs(cJSON_Parse(R"JSON({"x":7})JSON"));
        ASSERT_EQ(Value::t_json, rhs.kind);
        cJSON *updated = assign("$.a", doc, rhs);
        ASSERT_EQ(doc, updated);
    }

    EXPECT_EQ(std::string(R"JSON({"a":{"x":7}})JSON"), jsonToString(doc));
    cJSON_Delete(doc);
}

TEST(JsonOwnership, AssignFromTemporaryJsonValueKeepsDocumentValid) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);

    cJSON *updated = assign("$.a", doc, Value(cJSON_Parse(R"JSON({"x":9})JSON")));
    ASSERT_EQ(doc, updated);

    EXPECT_EQ(std::string(R"JSON({"a":{"x":9}})JSON"), jsonToString(doc));
    cJSON_Delete(doc);
}

TEST(JsonOwnership, AssignCloneKeepsSourceOwnedByCaller) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);
    cJSON *source = cJSON_Parse(R"JSON({"x":11})JSON");
    ASSERT_NE(nullptr, source);

    cJSON *updated = assign_clone("$.a", doc, source);
    ASSERT_EQ(doc, updated);

    EXPECT_EQ(std::string(R"JSON({"a":{"x":11}})JSON"), jsonToString(doc));
    EXPECT_EQ(std::string(R"JSON({"x":11})JSON"), jsonToString(source));

    cJSON_Delete(source);
    cJSON_Delete(doc);
}

TEST(JsonOwnership, AssignTakePathErrorDoesNotMutateDocument) {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    GTEST_SKIP() << "Boost.Context exception path reports false positives under ASAN.";
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "Boost.Context exception path reports false positives under ASAN.";
#endif

    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);

    cJSON *payload = cJSON_Parse(R"JSON({"x":11})JSON");
    ASSERT_NE(nullptr, payload);

    cJSON *updated = assign_take("$.a[0]", doc, payload);
    ASSERT_EQ(doc, updated);
    EXPECT_EQ(std::string(R"JSON({"a":1})JSON"), jsonToString(doc));

    cJSON_Delete(doc);
}

} // namespace
