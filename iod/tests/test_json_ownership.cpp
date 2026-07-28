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

// Regression: apply() clones via Print+Parse. Callers that use DEFAULT when the
// field is JSON null must still free that clone (via Value), or live node count
// grows on every ITEM ${field} OF json DEFAULT ... with a null field.
TEST(JsonOwnership, ApplyJsonNullConsumedByValueDoesNotLeak) {
    const long live_before = cJSON_LiveNodeCount();
    cJSON *doc = cJSON_Parse(R"JSON({"a":null,"b":1})JSON");
    ASSERT_NE(nullptr, doc);

    for (int i = 0; i < 100; ++i) {
        cJSON *sub = apply("$.a", doc);
        ASSERT_NE(nullptr, sub);
        EXPECT_EQ(cJSON_NULL, sub->type);
        Value resolved(sub); // must free the null node
        EXPECT_EQ(Value::t_empty, resolved.kind);
        // DEFAULT path: empty means "use default" without retaining apply() tree
        if (resolved.kind == Value::t_empty) {
            resolved = Value("");
        }
        EXPECT_EQ(Value::t_string, resolved.kind);
    }

    cJSON_Delete(doc);
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);
}

TEST(JsonOwnership, ApplyMissingKeyIsNullptrNoLeak) {
    const long live_before = cJSON_LiveNodeCount();
    cJSON *doc = cJSON_Parse(R"JSON({"a":1})JSON");
    ASSERT_NE(nullptr, doc);

    cJSON *sub = apply("$.missing", doc);
    EXPECT_EQ(nullptr, sub);
    Value resolved(sub);
    EXPECT_EQ(Value::t_empty, resolved.kind);

    cJSON_Delete(doc);
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);
}

} // namespace
