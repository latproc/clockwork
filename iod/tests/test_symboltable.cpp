#include "gtest/gtest.h"
#include <symboltable.h>
#include <value.h>
#include <set_default_value_mixin.h>

namespace {

class SymbolTableTest : public ::testing::Test {
  protected:
    void SetUp() override {
        st.add("x", Value(1));
        st.add("y", Value(2));
        st.add("one", Value("one", Value::t_string));
        st.add("name", Value("name", Value::t_symbol));
    }
    SymbolTable st;
};

TEST(SymbolTable, AddToTable) {
    Value bool_value(true);
    Value string_value("string");
    Value symbol_value("symbol");
    SymbolTable st;
    EXPECT_TRUE(st.add("bool", bool_value)) << "boolen value can be added";
    EXPECT_TRUE(st.add("string", string_value)) << "string value can be added";
    EXPECT_TRUE(st.add("symbol", symbol_value)) << "symbol value can be added";
}

TEST_F(SymbolTableTest, ValuesAreAsExpected) {
    EXPECT_EQ(st.find("one").kind, Value::t_string);
    EXPECT_EQ(st.find("name").kind, Value::t_symbol);
    EXPECT_EQ(st.find("x").kind, Value::t_integer);
    EXPECT_EQ(st.find("unknown").kind, Value::t_empty);
    EXPECT_FALSE(st.isKeyword("Hat"));
    EXPECT_TRUE(st.isKeyword("NOW"));
    EXPECT_TRUE(st.isKeyword("CLOCK"));
    EXPECT_TRUE(st.isKeyword("SECONDS"));
    EXPECT_TRUE(st.isKeyword("MINUTE"));
    EXPECT_TRUE(st.isKeyword("HOUR"));
    EXPECT_TRUE(st.isKeyword("DAY"));
    EXPECT_TRUE(st.isKeyword("MONTH"));
    EXPECT_TRUE(st.isKeyword("YR"));
    EXPECT_TRUE(st.isKeyword("YEAR"));
    EXPECT_TRUE(st.isKeyword("TIMESEQ"));
    EXPECT_TRUE(st.isKeyword("LOCALTIME"));
    EXPECT_TRUE(st.isKeyword("UTCTIME"));
    EXPECT_TRUE(st.isKeyword("TIMEZONE"));
    EXPECT_TRUE(st.isKeyword("TIMESTAMP"));
    EXPECT_TRUE(st.isKeyword("UTCTIMESTAMP"));
    EXPECT_TRUE(st.isKeyword("ISOTIMESTAMP"));
    EXPECT_TRUE(st.isKeyword("RANDOM"));
    EXPECT_NE(st.lookup("NOW"), SymbolTable::Null);
    EXPECT_NE(st.lookup("CLOCK"), SymbolTable::Null);
    EXPECT_NE(st.lookup("SECONDS"), SymbolTable::Null);
    EXPECT_NE(st.lookup("MINUTE"), SymbolTable::Null);
    EXPECT_NE(st.lookup("HOUR"), SymbolTable::Null);
    EXPECT_NE(st.lookup("DAY"), SymbolTable::Null);
    EXPECT_NE(st.lookup("MONTH"), SymbolTable::Null);
    EXPECT_NE(st.lookup("YR"), SymbolTable::Null);
    EXPECT_NE(st.lookup("YEAR"), SymbolTable::Null);
    EXPECT_NE(st.lookup("TIMESEQ"), SymbolTable::Null);
    EXPECT_NE(st.lookup("LOCALTIME"), SymbolTable::Null);
    EXPECT_NE(st.lookup("UTCTIME"), SymbolTable::Null);
    EXPECT_NE(st.lookup("TIMEZONE"), SymbolTable::Null);
    EXPECT_NE(st.lookup("TIMESTAMP"), SymbolTable::Null);
    EXPECT_NE(st.lookup("UTCTIMESTAMP"), SymbolTable::Null);
    EXPECT_NE(st.lookup("ISOTIMESTAMP"), SymbolTable::Null);
    EXPECT_NE(st.lookup("RANDOM"), SymbolTable::Null);
    EXPECT_EQ(st.lookup("YR"), st.lookup("YEAR"));
}

TEST(SymbolTable, Tokeniser) {
    SymbolTable st;
    st.add("VARIABLE", Value("VARIABLE", Value::t_symbol));
    EXPECT_TRUE(st.exists(ClockworkToken::VARIABLE));
    // Shouldn't we expect to be able to lookup a token?
    // EXPECT_EQ(st.lookup(ClockworkToken::VARIABLE), st.lookup("VARIABLE"));
}

TEST(SymbolTable, SymbolValueAddsTokenId) {
    SymbolTable st;
    Value value("MySymbol", Value::t_symbol);
    EXPECT_NE(value.token_id, 0);
    st.add("MySymbol", value);
    EXPECT_TRUE(st.exists(value.token_id));
}

struct IOBase {
    struct Address {
        bool is_signed{};
    } address;
    void setValue(int32_t val) {setIntValue_was_called = true; }
    void setValue(uint32_t val) { setUintValue_was_called = true; }
    void turnOff() { turnOff_was_called = true;}
    bool is_signed() const { return address.is_signed; }

    bool setIntValue_was_called{};
    bool setUintValue_was_called{};
    bool turnOff_was_called{};
};

TEST(SymbolTable, SetsDefaultIOIntValue) {
    SymbolTable st;
    auto set_value = [&st](const std::string &property, const Value &val){ st.add(property, val); };
    st.add("default", Value(1));
    struct IOValue : public IOValueMixIn<IOBase> {} io_value;
    io_value.address.is_signed = true;
    set_default_value_and_update_io<IOBase>( set_value, st, &io_value);
    EXPECT_TRUE(io_value.setIntValue_was_called);
    EXPECT_FALSE(io_value.setUintValue_was_called);
    EXPECT_FALSE(io_value.turnOff_was_called);
    EXPECT_TRUE(st.lookup("VALUE") == Value(1));
}

TEST(SymbolTable, SetsDefaultIOUintValue) {
    SymbolTable st;
    auto set_value = [&st](const std::string &property, const Value &val){ st.add(property, val); };
    st.add("default", Value(2));
    struct IOValue : public IOValueMixIn<IOBase> {} io_value;
    io_value.address.is_signed = false;
    set_default_value_and_update_io<IOBase>( set_value, st, &io_value);
    EXPECT_FALSE(io_value.setIntValue_was_called);
    EXPECT_TRUE(io_value.setUintValue_was_called);
    EXPECT_FALSE(io_value.turnOff_was_called);
    EXPECT_TRUE(st.lookup("VALUE") == Value(2));
}

TEST(SymbolTable, SetsDefaultIOOffWhenNoDefaultIsSet) {
    SymbolTable st;
    auto set_value = [&st](const std::string &property, const Value &val){ st.add(property, val); };
    struct IOValue : public IOValueMixIn<IOBase> {} io_value;
    set_default_value_and_update_io<IOBase>( set_value, st, &io_value);
    EXPECT_FALSE(io_value.setIntValue_was_called);
    EXPECT_FALSE(io_value.setUintValue_was_called);
    EXPECT_TRUE(io_value.turnOff_was_called);
    EXPECT_TRUE(st.lookup("VALUE") == SymbolTable::Null);
}

TEST(SymbolTable, SetsDefaultIOOffWhenDefaultIsNotAnInteger) {
    SymbolTable st;
    auto set_value = [&st](const std::string &property, const Value &val){ st.add(property, val); };
    st.add("default", Value("string"));
    struct IOValue : public IOValueMixIn<IOBase> {} io_value;
    set_default_value_and_update_io<IOBase>( set_value, st, &io_value);
    EXPECT_FALSE(io_value.setIntValue_was_called);
    EXPECT_FALSE(io_value.setUintValue_was_called);
    EXPECT_TRUE(io_value.turnOff_was_called);
    EXPECT_TRUE(st.lookup("VALUE") == Value("string"));
}

} // namespace
