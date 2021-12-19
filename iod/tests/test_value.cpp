#include "gtest/gtest.h"
#include <value.h>
#include <symboltable.h>

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

	TEST(Value, SymbolTableNullIsNull) {
		Value val;
		EXPECT_TRUE(val.isNull());
		EXPECT_EQ(val, SymbolTable::Null);
	}

	TEST_F(ValueTest, Integer) {
		EXPECT_EQ(Value::t_integer, x.kind) << "Value has an integer kind when assigned an integer";
		EXPECT_EQ(X_VALUE, x.iValue) << "Integer value is correctly assigned";
	}

	TEST_F(ValueTest, asInteger) {
		long v;
		EXPECT_EQ(true, x.asInteger(v));
		EXPECT_EQ(X_VALUE,v);
		EXPECT_EQ(true, y.asInteger(v));
		EXPECT_EQ(Y_VALUE,v);
	}

	TEST_F(ValueTest, AddInteger) {
		EXPECT_EQ(Value(X_VALUE + 87), x + 87);
		EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when +() is called";
		EXPECT_EQ(Value(Y_VALUE), y) << "Y is still unchanged";
		EXPECT_EQ(Value("1011"), y + 1) << "A string plus an integer appends the integer as a string";
		EXPECT_EQ(Value::t_string, (y+1).kind) << "A string plus an integer is a string";
		EXPECT_EQ(Value(X_VALUE + Y_VALUE), x + y) << "An integer plus a string adds the integer value of the string";
		EXPECT_EQ(Value("101413"), y + x) << "A string plus an integer value appends the value to the string";
	}

	TEST_F(ValueTest, SubtractInteger) {
		EXPECT_EQ(Value(X_VALUE - 87), x - 87);
		EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when -() is called";
		EXPECT_EQ(Value(y), y - 1) << "A string minus an integer returns the original value";
		EXPECT_EQ(Value::t_string, (y-1).kind) << "A string minus an integer is a string";
		EXPECT_EQ(Value(X_VALUE - Y_VALUE), x - y) << "An integer minus a string is an integer";
	}

	TEST_F(ValueTest, TimesInteger) {
		EXPECT_EQ(Value(X_VALUE * 23), x * 23);
		EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when *() is called";
		EXPECT_EQ(Value(y), y * 3) << "A string times an integer returns the original value";
		EXPECT_EQ(Value::t_string, (y*5).kind) << "A string times an integer is a string";
		EXPECT_EQ(Value(X_VALUE * Y_VALUE), x * y) << "An integer times a string is an integer";
		EXPECT_EQ(Value::t_integer, (x * y).kind) << "An integer times a string is an integer";
	}

	TEST_F(ValueTest, DivideInteger) {
		EXPECT_EQ(Value(X_VALUE / 3), x / 3);
		EXPECT_EQ(Value(X_VALUE), x) << "A value isn't changed when /() is called";
		EXPECT_EQ(Value(y), y / 3) << "A string divided by an integer returns the original value";
		EXPECT_EQ(Value::t_string, (y/5).kind) << "A string divided by an integer is a string";
		EXPECT_EQ(Value(X_VALUE / Y_VALUE), x / y) << "An integer divided by a string is an integer";
		EXPECT_EQ(Value::t_integer, (x / y).kind) << "An integer divided by a string is an integer";
	}

}
