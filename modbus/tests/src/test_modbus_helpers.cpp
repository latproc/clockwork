#include <gtest/gtest.h>
#include "modbus_helpers.h"

TEST(SerialSettings, constructor) {
    SerialSettings settings(2400,7,'E',2);

    EXPECT_TRUE(settings.baud==2400 && settings.bits == 7 && settings.parity == 'E' && settings.stop_bits == 2)
        << "initialises fields: "
        << settings.baud << "," << settings.bits << "," << settings.parity << "," << settings.stop_bits << "\n";
}

TEST(getSettings, withAnEmptyString) {
    SerialSettings settings;
    const char *config;
    int res;

    config = "";
    settings.baud = 1234;
    settings.bits = 4;
    settings.parity = 'X';
    settings.stop_bits = 5;
    res = getSettings(config, settings);
    EXPECT_EQ(0, res) << "returns zero for an empty tty setting string";

    EXPECT_TRUE(settings.baud==1234 && settings.bits == 4 && settings.parity == 'X' && settings.stop_bits == 5)
        << "does not change settings when an empty string is provided: "
        << settings.baud << "," << settings.bits << "," << settings.parity << "," << settings.stop_bits << "\n";
}

TEST(getSettings, withAValidString) {
    SerialSettings settings;
    const char *config;
    int res;
    settings.baud = 1234;
    settings.bits = 4;
    settings.parity = 'X';
    settings.stop_bits = 5;
    config = "19200:8:N:1";
    res = getSettings(config, settings);
    EXPECT_EQ(0, res) << "returns zero for a valid tty setting";
    EXPECT_EQ(19200, settings.baud) << "sets the baud rate";
    EXPECT_EQ(8, settings.bits) << "sets the bits";
    EXPECT_EQ('N', settings.parity) << "sets the parity";
    EXPECT_EQ(1,settings.stop_bits) << "sets the stop bits";
}
