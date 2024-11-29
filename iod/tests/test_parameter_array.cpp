#include <library_globals.cpp>
#include <gtest/gtest.h>
#include <parameter_array.h>

TEST(ToLimitedString, ReturnsEmptyStringForEmptyArray) {
    ParameterArray parameters;
    std::string str = toLimitedString(parameters);
    EXPECT_EQ(str, "[]");
}

TEST(ToLimitedString, ReturnsStringForSingleElementArray) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test"));
    std::string str = toLimitedString(parameters);
    EXPECT_EQ(str, "[test]");
}

TEST(ToLimitedString, ReturnsStringForMultipleElementArray) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test"));
    parameters.push_back(Parameter("test2"));
    std::string str = toLimitedString(parameters);
    EXPECT_EQ(str, "[test,test2]");
}

TEST(ToLimitedString, ReturnsStringForLimitedArray) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test"));
    std::string str = toLimitedString(parameters, 6);
    EXPECT_EQ(str, "[test]");
}

TEST(ToLimitedString, AddsEllipsisWhenTruncated) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test1"));
    parameters.push_back(Parameter("test2"));
    std::string str = toLimitedString(parameters, 6);
    EXPECT_EQ(str, "[t...]");
}

TEST(ToLimitedString, AddsEllipsisWhenTruncatedToSingleElement) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test1"));
    parameters.push_back(Parameter("test2"));
    std::string str = toLimitedString(parameters, 5);
    EXPECT_EQ(str, "[...]");
}

TEST(ToLimitedString, AddsEllipsisWhenTruncatedToOneCharacterMoreThanElement) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test1"));
    parameters.push_back(Parameter("test2"));
    parameters.push_back(Parameter("test3"));
    std::string str = toLimitedString(parameters, 14);
    EXPECT_EQ(str, "[test1,tes...]");
}

TEST(ToLimitedString, AddsEllipsisWhenTruncatedToOneCharacterMoreThanFirstElement) {
    ParameterArray parameters;
    parameters.push_back(Parameter("test1"));
    parameters.push_back(Parameter("test2"));
    std::string str = toLimitedString(parameters, 7);
    EXPECT_EQ(str, "[te...]");
}
