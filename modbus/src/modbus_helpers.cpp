#include "modbus_helpers.h"
#include <MessagingInterface.h>
#include <iostream>
#include <modbus.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <value.h>

void setSocketLinger0(zmq::socket_t &sock) {
    int linger = 0;
    try {
        sock.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    catch (const zmq::error_t &) {
    }
}

bool sendWithDeadline(zmq::socket_t &sock, const std::string &msg, std::string &response,
                      int64_t timeout_ms) {
    try {
        safeSend(sock, msg.c_str(), msg.size());
    }
    catch (const zmq::error_t &) {
        std::cerr << "modbus REQ send failed: " << zmq_strerror(zmq_errno()) << "\n";
        return false;
    }

    const uint64_t deadline = microsecs() + (uint64_t)timeout_ms * 1000ULL;
    while (microsecs() < deadline) {
        int64_t remain_ms = (int64_t)((deadline - microsecs()) / 1000ULL);
        if (remain_ms < 1) {
            remain_ms = 1;
        }
        if (remain_ms > 200) {
            remain_ms = 200;
        }
        try {
            zmq::pollitem_t items[] = {{(void *)sock, 0, ZMQ_POLLIN, 0}};
            int n = zmq::poll(items, 1, (long)remain_ms);
            if (n > 0 && (items[0].revents & ZMQ_POLLIN)) {
                char *buf = nullptr;
                size_t len = 0;
                if (safeRecv(sock, &buf, &len, false, 0)) {
                    response = buf ? buf : "";
                    delete[] buf;
                    return true;
                }
            }
        }
        catch (const zmq::error_t &) {
            if (zmq_errno() == EINTR) {
                continue;
            }
            std::cerr << "modbus REQ recv failed: " << zmq_strerror(zmq_errno()) << "\n";
            return false;
        }
    }
    std::cerr << "modbus REQ timed out after " << timeout_ms << "ms\n";
    return false;
}

std::string show_modbus_error(int rc) {
    std::stringstream result;
    if (rc >= MODBUS_ENOBASE) {
        if (rc < MODBUS_ENOBASE + MODBUS_EXCEPTION_MAX) {
            result << modbus_strerror(rc);
            rc = rc - MODBUS_ENOBASE;
        }
        else {
            rc = rc - MODBUS_ENOBASE - MODBUS_EXCEPTION_MAX;
            result << "device error:";
        }
    }
    else {
        result << strerror(rc);
    }
    result << " (" << rc << " == 0x" << std::hex << rc << std::dec << ")";
    return result.str();
}

int getSettings(const char *str, SerialSettings &settings) {
    int result = 0;
    char *buf = strdup(str);
    char *fld, *p = buf;
    enum SettingsStates { cs_baud, cs_bits, cs_parity, cs_stop, cs_end } state = cs_baud;

    while (state != cs_end) {
        fld = strsep(&p, ":");
        if (!fld)
            goto done_getSettings; // no more fields
        if (*fld == 0)
            continue; // skip blank fields

        char *tmp = 0;
        // most fields are numbers so we usually attempt to convert the field to a number
        long val = 8;
        if (state != cs_parity) {
            val = strtol(fld, &tmp, 10);
            if (tmp == 0) {
                result = -1; // no valid conversion
                break;
            }
            if (*tmp && *tmp != ':') {
                result = 1; // unexpected trailing data
                break;
            }
        }
        switch (state) {
        case cs_baud:
            settings.baud = val;
            state = cs_bits;
            break;
        case cs_bits:
            if (val == 8)
                settings.bits = 8;
            else if (val == 7)
                settings.bits = 7;
            state = cs_parity;
            break;
        case cs_parity:
            settings.parity = toupper(*fld);
            state = cs_stop;
            break;
        case cs_stop:
            if (val == 2)
                settings.stop_bits = 2;
            else if (val == 1)
                settings.stop_bits = 1;
            break;
            state = cs_end;
        case cs_end:
        default:;
        }
    }
done_getSettings:
    free(buf);
    return result;
}

bool isPrintable(const char *str) {
    if (!str)
        return false;
    while (*str)
        if (!isprint(*str++)) {
            return false;
        }
    return true;
}
