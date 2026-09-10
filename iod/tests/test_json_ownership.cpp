#include "gtest/gtest.h"

#include <cJSON.h>
#include <json_expression.h>
#include <cstring>
#include <string>
#include <utility>
#include <value.h>
#include "MessageEncoding.h"

#include "library_globals.cpp"

namespace {

int countObjectKeys(cJSON *obj, const char *name) {
    int n = 0;
    for (cJSON *c = obj ? obj->child : nullptr; c; c = c->next) {
        if (c->string && strcmp(c->string, name) == 0) {
            ++n;
        }
    }
    return n;
}

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

// Regression: apply() returns an owned clone (via clone_json / cJSON_Duplicate).
// Callers that use DEFAULT when the field is JSON null must still free that
// clone (via Value), or live node count grows on every
// ITEM ${field} OF json DEFAULT ... with a null field.
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
            resolved = Value("", Value::t_string);
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

// apply() must return a deep clone, not a borrowed pointer into the source doc.
TEST(JsonOwnership, ApplyReturnsIndependentClone) {
    cJSON *doc = cJSON_Parse(R"JSON({"a":{"x":1,"y":[2,3]},"b":"s"})JSON");
    ASSERT_NE(nullptr, doc);

    cJSON *sub = apply("$.a", doc);
    ASSERT_NE(nullptr, sub);
    ASSERT_NE(sub, cJSON_GetObjectItem(doc, "a"));

    cJSON *x = cJSON_GetObjectItem(sub, "x");
    ASSERT_NE(nullptr, x);
    EXPECT_EQ(1, (int)x->valueint);

    // Mutating the clone must not change the source document.
    cJSON_ReplaceItemInObject(sub, "x", cJSON_CreateNumber(99));
    cJSON *src_x = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "a"), "x");
    ASSERT_NE(nullptr, src_x);
    EXPECT_EQ(1, (int)src_x->valueint);

    cJSON_Delete(sub);
    cJSON_Delete(doc);
}

// Scalar/null clones from apply() (same ownership class as Value::getFromJSON)
// must be freed when converted through Value(cJSON*).
TEST(JsonOwnership, ValueGetFromJSONScalarDoesNotLeak) {
    const long live_before = cJSON_LiveNodeCount();
    cJSON *doc = cJSON_Parse(R"JSON({"n":1,"s":"x","z":null,"b":true})JSON");
    ASSERT_NE(nullptr, doc);

    for (int i = 0; i < 200; ++i) {
        {
            Value n(apply("$.n", doc));
            EXPECT_EQ(Value(1), n);
        }
        {
            Value s(apply("$.s", doc));
            EXPECT_EQ(Value::t_string, s.kind);
        }
        {
            Value z(apply("$.z", doc));
            EXPECT_EQ(Value::t_empty, z.kind);
        }
        {
            Value b(apply("$.b", doc));
            EXPECT_EQ(Value(true), b);
        }
        {
            Value m(apply("$.missing", doc));
            EXPECT_EQ(Value::t_empty, m.kind);
        }
    }

    cJSON_Delete(doc);
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);
}

// Regression: addValueToJSONObject / addValueToJSONArray must not fall through
// from the t_json case into t_bool (a missing `break`). Falling through appended
// a spurious `false` under the same key, corrupting every JSON value serialized
// into a command (e.g. dbd routing the QUERY reply payload to `response`).
TEST(JsonOwnership, AddJsonValueToObjectEmitsSingleValue) {
    Value arr(cJSON_Parse(R"JSON([{"id":1},{"id":2}])JSON"));
    ASSERT_EQ(Value::t_json, arr.kind);

    cJSON *obj = cJSON_CreateObject();
    MessageEncoding::addValueToJSONObject(obj, "value", arr);
    EXPECT_EQ(1, countObjectKeys(obj, "value"));
    cJSON *v = cJSON_GetObjectItem(obj, "value");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(cJSON_Array, v->type);
    cJSON_Delete(obj);
}

TEST(JsonOwnership, AddJsonValueToArrayEmitsSingleItem) {
    Value objv(cJSON_Parse(R"JSON({"id":1})JSON"));
    ASSERT_EQ(Value::t_json, objv.kind);

    cJSON *arr = cJSON_CreateArray();
    MessageEncoding::addValueToJSONArray(arr, objv);
    EXPECT_EQ(1, cJSON_GetArraySize(arr));
    cJSON_Delete(arr);
}

// Full round trip: a JSON array routed through encodeCommand (as dbd does for
// the `select` reply) must carry exactly one "value" — the row array, no bool.
TEST(JsonOwnership, EncodeCommandJsonParamHasSingleValue) {
    Value rows(cJSON_Parse(R"JSON([{"id":1},{"id":2}])JSON"));
    std::string cmd =
        MessageEncoding::encodeCommand("PROPERTY", Value("ed"), Value("response"), rows);

    cJSON *msg = cJSON_Parse(cmd.c_str());
    ASSERT_NE(nullptr, msg);
    cJSON *params = cJSON_GetObjectItem(msg, "params");
    ASSERT_NE(nullptr, params);
    ASSERT_EQ(3, cJSON_GetArraySize(params));
    cJSON *third = cJSON_GetArrayItem(params, 2);
    ASSERT_NE(nullptr, third);
    cJSON *type = cJSON_GetObjectItem(third, "type");
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(std::string("JSON"), std::string(type->valuestring));
    EXPECT_EQ(1, countObjectKeys(third, "value"));
    cJSON_Delete(msg);
}

} // namespace
