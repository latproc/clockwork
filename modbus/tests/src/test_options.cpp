#include <gtest/gtest.h>
#include "options.h"

TEST( Options, Constructor) {
    Options options;
    EXPECT_FALSE(options.verbose);
    EXPECT_TRUE(options.multireg_write);
    EXPECT_TRUE(options.singlereg_write);
}

// int main(int argc, char* argv[])
// {
//     testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
