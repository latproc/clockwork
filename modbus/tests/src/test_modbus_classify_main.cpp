#include "modbus_helpers.h"
#include <cerrno>
#include <iostream>
#include <modbus.h>

static int fails = 0;

static void expect_kind(const char *name, int rc, ModbusType mt, ModbusIoErrorKind want) {
    ModbusIoErrorKind got = classify_modbus_io_error(rc, mt);
    if (got != want) {
        std::cerr << "FAIL " << name << " rc=" << rc << "\n";
        ++fails;
    }
}

static void expect_eq(const char *name, unsigned got, unsigned want) {
    if (got != want) {
        std::cerr << "FAIL " << name << " got=" << got << " want=" << want << "\n";
        ++fails;
    }
}

int main() {
    expect_kind("rtu timeout", ETIMEDOUT, mt_RTU, ModbusIoErrorKind::Transient);
    expect_kind("rtu eagain", EAGAIN, mt_RTU, ModbusIoErrorKind::Transient);
    expect_kind("rtu eintr", EINTR, mt_RTU, ModbusIoErrorKind::Transient);
    expect_kind("rtu proto", MODBUS_ENOBASE + 1, mt_RTU, ModbusIoErrorKind::Transient);
    expect_kind("rtu ebadf", EBADF, mt_RTU, ModbusIoErrorKind::LinkDead);
    expect_kind("rtu eio", EIO, mt_RTU, ModbusIoErrorKind::LinkDead);
    expect_kind("rtu epipe", EPIPE, mt_RTU, ModbusIoErrorKind::LinkDead);
    expect_kind("tcp timeout", ETIMEDOUT, mt_TCP, ModbusIoErrorKind::LinkDead);

    expect_eq("success restores min", next_modbus_poll_interval_us(true, 2000000, 100000, 2000000),
              100000);
    expect_eq("fail doubles", next_modbus_poll_interval_us(false, 100000, 100000, 2000000), 200000);
    expect_eq("fail caps", next_modbus_poll_interval_us(false, 2000000, 100000, 2000000), 2000000);

    if (fails) {
        std::cerr << fails << " failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
