#include <gtest/gtest.h>
#include "options.h"
#include "modbus_helpers.h"

TEST( Options, Constructor) {
    Options options;
    EXPECT_FALSE(options.verbose);
    EXPECT_TRUE(options.multireg_write);
    EXPECT_TRUE(options.singlereg_write);
}

TEST( Options, RTU) {
    Options options;
    const char *argv[] = { "prog", "--tty", "myterminal" };
    int argc = sizeof(argv) / sizeof(const char *);
    EXPECT_EQ(argc, 3);
    EXPECT_TRUE(options.parseArgs(argc, argv)) << "parses sensible options";
    const ModbusSettings *ms = options.settings();
    EXPECT_EQ(mt_RTU, ms->mt);
}

TEST( Options, TCP) {
    Options options;
    const char *argv[] = { "prog", "-h", "myhost" };
    int argc = sizeof(argv) / sizeof(const char *);
    EXPECT_EQ(argc, 3);
    EXPECT_TRUE(options.parseArgs(argc, argv)) << "parses sensible options";
    const ModbusSettings *ms = options.settings();
    EXPECT_EQ(mt_TCP, ms->mt);
}

TEST( Options, Timeout) {
    Options options;
    const char *argv[] = { "prog", "--timeout", "1234" };
    int argc = sizeof(argv) / sizeof(const char *);
    EXPECT_TRUE(options.parseArgs(argc, argv)) << "parses options with timeout";
    EXPECT_EQ(options.getTimeout(), 1234);
}
