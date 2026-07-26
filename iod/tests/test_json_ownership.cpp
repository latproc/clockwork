#include "gtest/gtest.h"

#include <cJSON.h>
#include <json_expression.h>
#include <utility>
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

    OwnedJson payload = own_json(cJSON_Parse(R"JSON({"x":11})JSON"));
    ASSERT_NE(nullptr, payload.get());

    cJSON *updated = assign_take("$.a[0]", doc, std::move(payload));
    ASSERT_EQ(nullptr, payload.get());
    ASSERT_EQ(doc, updated);
    EXPECT_EQ(std::string(R"JSON({"a":1})JSON"), jsonToString(doc));

    cJSON_Delete(doc);
}

TEST(JsonOwnership, AssignTakeRootReplacementConsumesOwnedPayload) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);
    OwnedJson payload = own_json(cJSON_Parse(R"JSON({"z":5})JSON"));
    ASSERT_NE(nullptr, payload.get());

    cJSON *updated = assign_take("$", doc, std::move(payload));
    ASSERT_EQ(nullptr, payload.get());
    ASSERT_NE(nullptr, updated);
    EXPECT_NE(doc, updated);
    EXPECT_EQ(std::string(R"JSON({"z":5})JSON"), jsonToString(updated));

    cJSON_Delete(updated);
}

TEST(JsonOwnership, AssignTakeEmptyPayloadIsNoOp) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);
    const long live_before = cJSON_LiveNodeCount();

    cJSON *updated = assign_take("$.a", doc, OwnedJson{});
    EXPECT_EQ(doc, updated);
    EXPECT_EQ(std::string(R"JSON({"a":1})JSON"), jsonToString(doc));
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);

    cJSON_Delete(doc);
}

TEST(JsonOwnership, AssignValueClonesBoolEmptyAndJsonKinds) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":1,"b":2,"c":3})JSON");
    ASSERT_NE(nullptr, doc);
    const long live_before = cJSON_LiveNodeCount();

    EXPECT_EQ(doc, assign("$.a", doc, Value(true)));
    EXPECT_EQ(doc, assign("$.b", doc, Value()));
    {
        Value nested(cJSON_Parse(R"JSON({"x":9})JSON"));
        EXPECT_EQ(doc, assign("$.c", doc, nested));
        EXPECT_EQ(nested.kind, Value::t_json);
    }

    EXPECT_EQ(std::string(R"JSON({"a":true,"b":null,"c":{"x":9}})JSON"), jsonToString(doc));
    cJSON_Delete(doc);
    EXPECT_LE(cJSON_LiveNodeCount(), live_before);
}

} // namespace
