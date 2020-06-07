#include "gtest/gtest.h"

std::string constructAlphaNumericString(const char *prefix, const char *val, const char *suffix, const char *default_name);

TEST( constructAlphaNumericString, ReturnsDefaultIfNull) {
		std::string str = constructAlphaNumericString(nullptr, nullptr, nullptr, "XXy");
    EXPECT_EQ(str, "XXy");
}

TEST( constructAlphaNumericString, ReturnsDefaultIfNoValue) {
		std::string str = constructAlphaNumericString("XX_", nullptr, ".dat", "XXy");
    EXPECT_EQ(str, "XXy");
}

TEST( constructAlphaNumericString, ReturnsDefaultIfValueHasNoAlphaNums) {
		std::string str = constructAlphaNumericString("XX_", "#-^.", ".dat", "XXy");
    EXPECT_EQ(str, "XXy");
}

TEST( constructAlphaNumericString, ReturnsValueIfValueIsAlphaNums) {
		std::string str = constructAlphaNumericString(nullptr, "4AbC1", nullptr, "XXy");
    EXPECT_EQ(str, "4AbC1");
}

TEST( constructAlphaNumericString, StripsNonAlphaNumIfValueHasNonAlphaNums) {
		std::string str = constructAlphaNumericString(nullptr, "4Ab_C1", nullptr, "XXy");
    EXPECT_EQ(str, "4AbC1");
}

TEST( constructAlphaNumericString, AddsPrefixIfGiven) {
		std::string str = constructAlphaNumericString("a3_", "'4Ab_C1", nullptr, "XXy");
    EXPECT_EQ(str, "a3_4AbC1");
}

TEST( constructAlphaNumericString, AddsSuffixIfGiven) {
		std::string str = constructAlphaNumericString(nullptr, "'4Ab_C1", "_1", "XXy");
    EXPECT_EQ(str, "4AbC1_1");
}

TEST( constructAlphaNumericString, AddsPrefixAndSuffixIfGiven) {
		std::string str = constructAlphaNumericString("z_", "'4Ab_C1", "_1", "XXy");
    EXPECT_EQ(str, "z_4AbC1_1");
}
