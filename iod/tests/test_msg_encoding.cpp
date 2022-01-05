#include "library_globals.c"
#include "gtest/gtest.h"
#include <MessageEncoding.h>
#include <cJSON.h>

TEST(EncodeValue, EncodesBoolean) {
    cJSON *obj = cJSON_CreateObject();
    MessageEncoding::addValueToJSONObject(obj, "x", false);
    Value val = MessageEncoding::valueFromJSONObject(cJSON_GetObjectItem(obj, "x"), nullptr);
    EXPECT_EQ(val.kind, Value::t_bool);
    EXPECT_EQ(val.bValue, false);
    cJSON_Delete(obj);
}

TEST(EncodeValue, EncodesNull) {
    cJSON *obj = cJSON_CreateObject();
    MessageEncoding::addValueToJSONObject(obj, "x", Value());
    Value val = MessageEncoding::valueFromJSONObject(cJSON_GetObjectItem(obj, "x"), nullptr);
    EXPECT_EQ(val.kind, Value::t_empty);
    EXPECT_EQ(val.bValue, false);
    cJSON_Delete(obj);
}
