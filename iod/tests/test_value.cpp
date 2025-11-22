#include "gtest/gtest.h"
#include <symboltable.h>
#include <value.h>
#include "cJSON.h"
#include <cmath>
#include <utility>

namespace {

class ValueTest : public ::testing::Test {
  protected:
    const int X_VALUE = 413;
    const int Y_VALUE = 101;

    void SetUp() override {
        x = X_VALUE;
        y = Value(std::to_string(Y_VALUE), Value::t_string);
    }
    Value x;
    Value y;
};

class TrackedDynamicValue : public DynamicValueBase {
  public:
    explicit TrackedDynamicValue(int &destructions) : destructions_(destructions) {}
    ~TrackedDynamicValue() override { ++destructions_; }

    DynamicValueBase *clone() const override { return nullptr; }
    const Value *lastResult() const override { return nullptr; }
    void setScope(MachineInstance *) override {}
    MachineInstance *getScope() const override { return nullptr; }
    std::ostream &operator<<(std::ostream &out) const override { return out; }
    const Value &operator()(MachineInstance *) override { return result_; }
    const Value &operator()() override { return result_; }
    int references() const { return refs; }

  private:
    int &destructions_;
    Value result_;
};

TEST(Value, InitializesInactiveScalarFields) {
    Value empty;
    EXPECT_FALSE(empty.bValue);
    EXPECT_EQ(empty.iValue, 0);
    EXPECT_DOUBLE_EQ(empty.fValue, 0.0);
}

TEST(Value, CompareBool) {
    Value val(true);
    EXPECT_EQ(val.kind, Value::t_bool) << "Value has a bool kind when assigned a bool";
    EXPECT_TRUE(val == true) << "Value is true when assigned a true";
    val = false;
    EXPECT_TRUE(val == false) << "Value is false when assigned a false";
    val = "FALSE";
    EXPECT_EQ(val.kind, Value::t_bool) << "symbol FALSE evaluates as a bool";
    val = "TRUE";
    EXPECT_EQ(val.kind, Value::t_bool) << "symbol TRUE evaluates as a bool";
}

TEST(Value, MoveTransfersDynamicValueWithoutRetainingAnExtraReference) {
    int destructions = 0;
    {
        auto *tracked = new TrackedDynamicValue(destructions);
        Value source(tracked);
        EXPECT_EQ(tracked->references(), 1);
        Value destination(std::move(source));

        EXPECT_EQ(source.kind, Value::t_empty);
        EXPECT_NE(destination.dynamicValue(), nullptr);
        EXPECT_EQ(tracked->references(), 1);
    }
    EXPECT_EQ(destructions, 1);
}

TEST(Value, CopyRetainsDynamicValueReference) {
    int destructions = 0;
    {
        auto *tracked = new TrackedDynamicValue(destructions);
        Value source(tracked);
        EXPECT_EQ(tracked->references(), 1);
        {
            Value destination(source);
            EXPECT_EQ(destination.dynamicValue(), tracked);
            EXPECT_EQ(tracked->references(), 2);
        }
        EXPECT_EQ(tracked->references(), 1);
        EXPECT_EQ(destructions, 0);
    }
    EXPECT_EQ(destructions, 1);
}

TEST(Value, SymbolTableNullIsNull) {
    Value val;
    EXPECT_TRUE(val.isNull());
    EXPECT_EQ(val, SymbolTable::Null);
}

TEST(Value, CanConstructWithJSON) {
    Value v(cJSON_CreateNull());
    EXPECT_EQ(v.asString(), std::string("null"));
}

TEST(Value, ConstructingFromJSONScalarTakesOwnership) {
    const long live_before = cJSON_LiveNodeCount();
    {
        Value v(cJSON_Parse("413"));
        EXPECT_EQ(v, Value(413));
    }
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);
}

TEST(Value, CanAssignJSON) {
    Value v;
    v = cJSON_CreateNull();
    EXPECT_EQ(v.asString(), std::string("null"));

    auto json_str = R"JSON({"a":1,"b":"hello"})JSON";
    v = cJSON_Parse(json_str);
    EXPECT_EQ(v.kind, Value::t_json);
}

TEST(Value, AssigningFromJSONScalarTakesOwnership) {
    Value v;
    const long live_before = cJSON_LiveNodeCount();
    v = cJSON_Parse("\"hello\"");
    EXPECT_EQ(v.asString(), std::string("hello"));
    EXPECT_EQ(cJSON_LiveNodeCount(), live_before);
}

TEST(Value, CanAssignJSONValue) {
    Value v;
    Value w(cJSON_CreateNull());
    v = w;
    EXPECT_EQ(v.asString(), std::string("null"));
}

TEST(Value, CanGetStringFromJSONObject) {
    Value v(cJSON_CreateObject());
    cJSON_AddItemToObject(v.json, "greeting", cJSON_CreateString("hello"));
    Value result = v.getFromJSON("greeting");
    EXPECT_EQ(result.asString(), std::string("hello"));
    EXPECT_EQ(result.kind, Value::t_string);
}

TEST(Value, CanGetFloatFromJSONObject) {
    Value v(cJSON_CreateObject());
    cJSON_AddItemToObject(v.json, "Pi", cJSON_CreateDouble(M_PI));
    Value result = v.getFromJSON("Pi");
    EXPECT_EQ(result, M_PI);
    EXPECT_EQ(result.kind, Value::t_float);
}

TEST(Value, CanGetIntFromJSONObject) {
    Value v(cJSON_CreateObject());
    cJSON_AddItemToObject(v.json, "answer", cJSON_CreateNumber(42));
    Value result = v.getFromJSON("answer");
    EXPECT_EQ(result, 42);
    EXPECT_EQ(result.kind,Value::t_integer);
}

TEST(Value, CanGetTrueFromJSONObject) {
    Value v(cJSON_CreateObject());
    cJSON_AddItemToObject(v.json, "truth", cJSON_CreateTrue());
    Value result = v.getFromJSON("truth");
    EXPECT_EQ(result, true);
    EXPECT_EQ(result.kind, Value::t_bool);
}

TEST(Value, CanGetFalseFromJSONObject) {
    Value v(cJSON_CreateObject());
    cJSON_AddItemToObject(v.json, "false", cJSON_CreateFalse());
    Value result = v.getFromJSON("false");
    EXPECT_EQ(result, false);
}

TEST_F(ValueTest, Integer) {
    EXPECT_EQ(Value::t_integer, x.kind) << "Value has an integer kind when assigned an integer";
    EXPECT_EQ(X_VALUE, x.iValue) << "Integer value is correctly assigned";
}

TEST_F(ValueTest, asInteger) {
    int64_t v;
    EXPECT_EQ(true, x.asInteger(v));
    EXPECT_EQ(X_VALUE, v);
    EXPECT_EQ(true, y.asInteger(v));
    EXPECT_EQ(Y_VALUE, v);
}

TEST_F(ValueTest, AddInteger) {
    EXPECT_EQ(Value(X_VALUE + 87), x + 87);
    EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when +() is called";
    EXPECT_EQ(Value(Y_VALUE), y) << "Y is still unchanged";
    EXPECT_EQ(Value("1011"), y + 1) << "A string plus an integer appends the integer as a string";
    EXPECT_EQ(Value::t_string, (y + 1).kind) << "A string plus an integer is a string";
    EXPECT_EQ(Value(X_VALUE + Y_VALUE), x + y)
        << "An integer plus a string adds the integer value of the string";
    EXPECT_EQ(Value("101413"), y + x)
        << "A string plus an integer value appends the value to the string";
}

TEST_F(ValueTest, SubtractInteger) {
    EXPECT_EQ(Value(X_VALUE - 87), x - 87);
    EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when -() is called";
    EXPECT_EQ(Value(y), y - 1) << "A string minus an integer returns the original value";
    EXPECT_EQ(Value::t_string, (y - 1).kind) << "A string minus an integer is a string";
    EXPECT_EQ(Value(X_VALUE - Y_VALUE), x - y) << "An integer minus a string is an integer";
}

TEST_F(ValueTest, TimesInteger) {
    EXPECT_EQ(Value(X_VALUE * 23), x * 23);
    EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when *() is called";
    EXPECT_EQ(Value(y), y * 3) << "A string times an integer returns the original value";
    EXPECT_EQ(Value::t_string, (y * 5).kind) << "A string times an integer is a string";
    EXPECT_EQ(Value(X_VALUE * Y_VALUE), x * y) << "An integer times a string is an integer";
    EXPECT_EQ(Value::t_integer, (x * y).kind) << "An integer times a string is an integer";
}

TEST_F(ValueTest, DivideInteger) {
    EXPECT_EQ(Value(X_VALUE / 3), x / 3);
    EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when /() is called";
    EXPECT_EQ(Value(y), y / 3) << "A string divided by an integer returns the original value";
    EXPECT_EQ(Value::t_string, (y / 5).kind) << "A string divided by an integer is a string";
    EXPECT_EQ(Value(X_VALUE / Y_VALUE), x / y) << "An integer divided by a string is an integer";
    EXPECT_EQ(Value::t_integer, (x / y).kind) << "An integer divided by a string is an integer";
}

} // namespace
