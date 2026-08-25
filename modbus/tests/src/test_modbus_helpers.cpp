#include <gtest/gtest.h>
#include "modbus_helpers.h"
#include <cerrno>
#include <modbus.h>

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

TEST(isPrintable, withAPrintableString) {
	EXPECT_TRUE(isPrintable("this is a test"));
}

TEST(isPrintable, withANonPrintableString) {
	EXPECT_FALSE(isPrintable("this \001 is a test"));
}

TEST(classify_modbus_io_error, rtuTimeoutIsTransient) {
    EXPECT_EQ(ModbusIoErrorKind::Transient, classify_modbus_io_error(ETIMEDOUT, mt_RTU));
    EXPECT_EQ(ModbusIoErrorKind::Transient, classify_modbus_io_error(EAGAIN, mt_RTU));
    EXPECT_EQ(ModbusIoErrorKind::Transient, classify_modbus_io_error(EINTR, mt_RTU));
    EXPECT_EQ(ModbusIoErrorKind::Transient,
              classify_modbus_io_error(MODBUS_ENOBASE + 1, mt_RTU));
}

TEST(classify_modbus_io_error, rtuBrokenFdIsLinkDead) {
    EXPECT_EQ(ModbusIoErrorKind::LinkDead, classify_modbus_io_error(EBADF, mt_RTU));
    EXPECT_EQ(ModbusIoErrorKind::LinkDead, classify_modbus_io_error(EIO, mt_RTU));
    EXPECT_EQ(ModbusIoErrorKind::LinkDead, classify_modbus_io_error(EPIPE, mt_RTU));
}

TEST(classify_modbus_io_error, tcpTimeoutIsLinkDead) {
    EXPECT_EQ(ModbusIoErrorKind::LinkDead, classify_modbus_io_error(ETIMEDOUT, mt_TCP));
}

TEST(next_modbus_poll_interval_us, restoresMinOnSuccess) {
    EXPECT_EQ(100000u, next_modbus_poll_interval_us(true, 2000000, 100000, 2000000));
}

TEST(next_modbus_poll_interval_us, doublesThenCaps) {
    EXPECT_EQ(200000u, next_modbus_poll_interval_us(false, 100000, 100000, 2000000));
    EXPECT_EQ(2000000u, next_modbus_poll_interval_us(false, 2000000, 100000, 2000000));
    EXPECT_EQ(2000000u, next_modbus_poll_interval_us(false, 1500000, 100000, 2000000));
}
