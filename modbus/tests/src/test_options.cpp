#include <gtest/gtest.h>
#include "monitor.h"

TEST( Options, Constructor) {
    Options options;
    EXPECT_EQ(options.verbose, false);
}

// int main(int argc, char* argv[])
// {
//     testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
