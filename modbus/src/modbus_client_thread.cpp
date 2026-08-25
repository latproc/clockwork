#include "src/modbus_client_thread.h"
#include "MessagingInterface.h"
#include "monitor.h"
#include "src/buffer_monitor.h"
#include "src/cw_interface.h"
#include "src/modbus_helpers.h"
#include "src/options.h"
#include <Logger.h>
#include <MessageEncoding.h>
#include <MessagingInterface.h>
#include <boost/thread.hpp>
#include <cerrno>
#include <iostream>
#include <libgen.h>
#include <map>
#include <modbus.h>
#include <set>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <value.h>
#include <zmq.hpp>

namespace {
const unsigned kPollMinUs = 100000;
const unsigned kPollMaxUs = 2000000;
} // namespace

void ModbusClientThread::requestUpdate(int addr, bool which) {
    boost::mutex::scoped_lock lock(work_mutex);
    bit_changes.push_back(std::make_pair(addr, which));
}

void ModbusClientThread::requestRegisterUpdate(int addr, uint16_t val) {
    boost::mutex::scoped_lock lock(work_mutex);
    register_changes.push_back(std::make_pair(addr, val));
}

void ModbusClientThread::requestRegisterUpdates(int addr, uint16_t *vals, size_t count) {
    boost::mutex::scoped_lock lock(work_mutex);
    for (size_t i = 0; i < count; ++i) {
        register_changes.push_back(std::make_pair(addr + i, *vals++));
    }
}

modbus_t *ModbusClientThread::getContext() {
    update_mutex.lock();
    return ctx;
}
void ModbusClientThread::releaseContext() { update_mutex.unlock(); }

void ModbusClientThread::performUpdates() {
    boost::mutex::scoped_lock lock(work_mutex);
    {
        std::list<std::pair<int, bool>>::iterator iter = bit_changes.begin();
        while (iter != bit_changes.end()) {
            std::pair<int, bool> item = *iter;
            int err_code = setBit(item.first, item.second);
            if (err_code != 0) {
                std::cerr << "setBit failed: " << show_modbus_error(err_code) << "\n";
            }
            iter = bit_changes.erase(iter);
        }
    }
    {
        std::list<std::pair<int, uint16_t>>::iterator iter = register_changes.begin();
        if (iter == register_changes.end())
            return;
        unsigned int space = 10;
        uint16_t *vals = (uint16_t *)malloc(space * sizeof(uint16_t));
        unsigned int n = 0;
        int start = 0;
        int last = 0;
        while (iter != register_changes.end()) {
            std::pair<int, uint16_t> item = *iter;
            if (n == 0) {
                start = item.first;
            }
            else if (item.first != last + 1) {
                //flush
                int err_code = setRegisters(start, vals, n);
                if (err_code != 0) {
                    std::cerr << "setBit failed: " << show_modbus_error(err_code) << "\n";
                }
                start = item.first;
                n = 0;
            }
            last = item.first;
            vals[n++] = item.second;
            if (n == space) {
                space += 10;
                auto new_vals = (uint16_t *)realloc(vals, space * sizeof(uint16_t));
                if (!new_vals) {
                    std::cerr << "realloc failed performing modbus updates\n";
                    free(vals);
                    return;
                }
                vals = new_vals;
            }
            iter = register_changes.erase(iter);
        }
        if (n) {
            int err_code = setRegisters(start, vals, n);
            if (err_code != 0) {
                std::cerr << "setBit failed: " << show_modbus_error(err_code) << "\n";
            }
        }
        free(vals);
    }
}

void ModbusClientThread::refresh() {
    bits_monitor.refresh();
    robits_monitor.refresh();
    regs_monitor.refresh();
    holdings_monitor.refresh();
}

ModbusClientThread::ModbusClientThread(const ModbusSettings &modbus_settings,
                                       MonitorConfiguration &modbus_config, const Options &options,
                                       bool &update_status, const char *sock_name)
    : ctx(0), tab_rp_bits(0), tab_ro_bits(0), tab_rq_registers(0), tab_rw_rq_registers(0),
      options(options), update_status(update_status), finished(false), connected(false),
      slave_silent_cycles(0), poll_interval_us(kPollMinUs), transient_error_logs(0),
      bits_monitor("coils", options), robits_monitor("discrete", options),
      regs_monitor("registers", options), holdings_monitor("holdings", options), mc(modbus_config),
      settings(modbus_settings), cmd_interface(0), iod_cmd_socket_name(sock_name) {
    boost::mutex::scoped_lock lock(update_mutex);

    ctx = openConnection();
    if (!ctx) {
        std::cerr << "failed to create modbus context - could not open connection\n";
        exit(1);
    }
    modbus_set_debug(ctx, FALSE);

    /* Allocate and initialize the different memory spaces */
    int nb = 10000;

    tab_rp_bits = (uint8_t *)malloc(nb * sizeof(uint8_t));
    memset(tab_rp_bits, 0, nb * sizeof(uint8_t));

    tab_ro_bits = (uint8_t *)malloc(nb * sizeof(uint8_t));
    memset(tab_ro_bits, 0, nb * sizeof(uint8_t));

    tab_rq_registers = (uint16_t *)malloc(nb * sizeof(uint16_t));
    memset(tab_rq_registers, 0, nb * sizeof(uint16_t));

    tab_rw_rq_registers = (uint16_t *)malloc(nb * sizeof(uint16_t));
    memset(tab_rw_rq_registers, 0, nb * sizeof(uint16_t));
}

modbus_t *ModbusClientThread::openConnection() {
    if (settings.mt == mt_TCP) {
        if (options.verbose)
            std::cerr << "opening tcp connection to " << settings.device_name << ":"
                      << settings.settings << "\n";
        int port = 1502;
        if (!ctx && strToInt(settings.settings.c_str(), port)) {
            ctx = modbus_new_tcp(settings.device_name.c_str(), port);
        }
        if (ctx == NULL) {
            std::cerr << "Unable to create the libmodbus context\n";
        }
        else if (modbus_connect(ctx) == -1) {
            int saved_errno = errno;
            std::cerr << "Connection to " << settings.device_name << ":" << settings.settings
                      << " failed: " << show_modbus_error(saved_errno) << "\n";
            modbus_free(ctx);
            ctx = 0;
        }
        else if (modbus_set_slave(ctx, settings.device_id) == -1) {
            std::cerr << "modbus_set_slave: " << modbus_strerror(errno) << "\n";
            modbus_free(ctx);
            ctx = 0;
        }
#if 0
        if (modbus_rtu_get_serial_mode(ctx) != MODBUS_RTU_RS485) {
            std::cerr << "setting RTU 485 mode\n";
            if (modbus_rtu_set_serial_mode(ctx, MODBUS_RTU_RS485) == -1) {
                std::cerr << "modbus_rtu_set_serial_mode: " << modbus_strerror(errno) << "\n";
            }
        }
        else {
            std::cerr << "defaulting to RTU 485 mode\n";
        }
#endif
    }
    else if (settings.mt == mt_RTU) {
        int device_id = *settings.devices.begin();
        std::cerr << "opening rtu connection to " << settings.device_name << ", device "
                  << device_id << "\n";
        ctx =
            modbus_new_rtu(settings.device_name.c_str(), settings.serial.baud,
                           settings.serial.parity, settings.serial.bits, settings.serial.stop_bits);
        if (ctx == NULL) {
            std::cerr << "Unable to create the libmodbus context\n";
        }
        else {
            if (settings.devices.size() > 0) {
                if (modbus_set_slave(ctx, device_id) == -1) {
                    std::cerr << "modbus_set_slave: " << modbus_strerror(errno) << "\n";
                    modbus_free(ctx);
                    ctx = 0;
                }
            }
            if (modbus_connect(ctx) == -1) {
                std::cerr << "Connection failed: " << modbus_strerror(errno) << "\n";
                modbus_free(ctx);
                ctx = 0;
            }
        }
    }
    if (!ctx) {
        sendStatus("disconnected");
        return 0;
    }
    /* Save original timeout */
    uint32_t secs, usecs;
    int rc = modbus_get_response_timeout(ctx, &secs, &usecs);
    if (rc == -1) {
        int saved_errno = errno;
        std::cerr << "modbus_get_response_timeout failed: " << show_modbus_error(saved_errno)
                  << "\n";
        modbus_free(ctx);
        ctx = 0;
        sendStatus("disconnected");
    }
    else {
        if (options.verbose)
            std::cerr << "original response timeout: " << secs << "." << std::setw(3)
                      << std::setfill('0') << (usecs / 1000) << "\n";
        uint64_t new_timeout = options.getTimeout();
        uint32_t new_secs = (new_timeout) ? static_cast<uint32_t>(new_timeout / 1000000) : secs * 2;
        uint32_t new_usecs = (new_timeout) ? static_cast<uint32_t>(new_timeout % 1000000) : usecs * 2;
        while (new_usecs >= 1000000) {
            new_usecs -= 1000000;
            new_secs++;
        }
        rc = modbus_set_response_timeout(ctx, new_secs, new_usecs);
        if (rc == -1) {
            perror("modbus_set_byte_timeout");
            sendStatus("disconnected");
            modbus_free(ctx);
            ctx = 0;
        }
        else {
            if (options.verbose) {
                std::cerr << "new timeout: " << new_secs << "." << std::setw(3) << std::setfill('0')
                          << (new_usecs / 1000) << "\n";
            }
        }
    }
    return ctx;
}

void ModbusClientThread::recreateCmdSocket() {
    if (cmd_interface) {
        setSocketLinger0(*cmd_interface);
        delete cmd_interface;
        cmd_interface = 0;
    }
    if (!iod_cmd_socket_name) {
        return;
    }
    try {
        cmd_interface = new zmq::socket_t(*MessagingInterface::getContext(), ZMQ_REQ);
        setSocketLinger0(*cmd_interface);
        cmd_interface->connect(iod_cmd_socket_name);
    }
    catch (const zmq::error_t &) {
        std::cerr << "mbmon ModbusClientThread failed to create inproc REQ: "
                  << zmq_strerror(zmq_errno()) << "\n";
        delete cmd_interface;
        cmd_interface = 0;
    }
}

bool ModbusClientThread::sendToIod(const std::string &msg, std::string &response) {
    if (!cmd_interface) {
        recreateCmdSocket();
    }
    if (!cmd_interface) {
        return false;
    }
    if (sendWithDeadline(*cmd_interface, msg, response, IOD_CMD_TIMEOUT_MS)) {
        return true;
    }
    recreateCmdSocket();
    return false;
}

bool ModbusClientThread::sendCommand(const std::string &cmd, const std::list<Value> &params) {
    if (params.empty()) {
        return false;
    }
    std::string msg = MessageEncoding::encodeCommand(cmd, params);
    if (options.verbose) {
        std::cerr << " sending: " << msg << "\n";
    }
    std::string response;
    if (!sendToIod(msg, response)) {
        return false;
    }
    if (options.verbose && !response.empty()) {
        std::cerr << response << "\n";
    }
    return true;
}

ModbusClientThread::~ModbusClientThread() {
    free(tab_rp_bits);
    free(tab_ro_bits);
    free(tab_rq_registers);
    free(tab_rw_rq_registers);
    if (cmd_interface) {
        setSocketLinger0(*cmd_interface);
        delete cmd_interface;
        cmd_interface = 0;
    }
    if (ctx) {
        modbus_close(ctx);
        modbus_free(ctx);
    }
}

void ModbusClientThread::close_connection() {
    if (options.verbose)
        std::cerr << " closing connection\n";
    sendStatus("disconnected");
    update_status = true;

    boost::mutex::scoped_lock lock(update_mutex);
    if (!ctx) {
        connected = false;
        return;
    }
    modbus_flush(ctx);
    modbus_close(ctx);
    modbus_free(ctx);
    connected = false;
    ctx = 0;
}

bool ModbusClientThread::check_error(int rc, const char *msg, int entry, int *retry) {
    const ModbusIoErrorKind kind = classify_modbus_io_error(rc, settings.mt);
    auto err_str = show_modbus_error(rc);
    if (kind == ModbusIoErrorKind::Transient) {
        ++transient_error_logs;
        if (transient_error_logs <= 3 || (transient_error_logs % 100) == 0) {
            fprintf(stderr, "%s %s, entry %d retrying %d (silent #%u)\n", msg, err_str.c_str(),
                    entry, *retry, transient_error_logs);
        }
        usleep(250);
        return true;
    }
    fprintf(stderr, "%s %s, entry %d disconnecting %d\n", msg, err_str.c_str(), entry, *retry);
    if (connected) {
        close_connection();
    }
    return false;
}

int ModbusClientThread::setBit(int addr, bool which) {
    std::cerr << "set bit " << addr << " to " << which << "\n";
    boost::mutex::scoped_lock lock(update_mutex);
    int rc = 0;
    int retries = 3; // TODO: Make this a setting
    while ((rc = modbus_write_bit(ctx, addr, (which) ? 1 : 0)) == -1) {
        int saved_errno = errno;
        std::cerr << "modbus_write_bit failed: " << show_modbus_error(saved_errno) << "\n";
        if (check_error(saved_errno, "modbus_write_bit", addr, &retries)) {
            if (!connected) {
                return saved_errno;
            }
            if (--retries > 0)
                continue;
        }
        else {
            return saved_errno;
        }
    }
    return 0;
}

// Attempt to set the given register. If an error occurs, retry up to 3 times.
int ModbusClientThread::setRegister(int addr, uint16_t val) {
    if (!connected)
        return ENXIO;
    if (options.verbose)
        std::cerr << "set register " << addr << " to " << val << "\n";
    boost::mutex::scoped_lock lock(update_mutex);
    int rc = 0;
    int retries = 3; // TODO: Make this a setting
    if (settings.support_single_register_write)
        rc = modbus_write_register(ctx, addr, val);
    else
        rc = modbus_write_registers(ctx, addr, 1, &val);
    while (rc == -1) {
        int saved_errno = errno;
        std::cerr << "modbus_write_register failed: " << show_modbus_error(saved_errno) << "\n";
        if (check_error(saved_errno, "modbus_write_register(s) ", addr, &retries)) {
            if (!connected) {
                return saved_errno;
            }
            if (--retries > 0) {
                if (settings.support_single_register_write)
                    rc = modbus_write_register(ctx, addr, val);
                else
                    rc = modbus_write_registers(ctx, addr, 1, &val);
                continue;
            }
            else {
                return saved_errno;
            }
        }
        else {
            return saved_errno;
        }
    }
    return 0;
}

// Attempt to set the given registers. If an error occurs, retry up to 3 times.
int ModbusClientThread::setRegisters(int addr, uint16_t *val, unsigned int n) {
    if (!connected) {
        return ENXIO;
    }
    if (options.verbose) {
        std::cerr << "set register " << addr << " to ";
        for (unsigned int i = 0; i < n; ++i)
            std::cerr << val[i] << " ";
        std::cerr << "\n";
    }
    boost::mutex::scoped_lock lock(update_mutex);
    int rc = 0;
    int retries = 3; // TODO: Make this a setting
    rc = modbus_write_registers(ctx, addr, static_cast<int>(n), val);
    while ((rc = modbus_write_registers(ctx, addr, static_cast<int>(n), val)) == -1) {
        int saved_errno = errno;
        std::cerr << "modbus_write_registers: " << show_modbus_error(saved_errno) << "\n";
        if (check_error(saved_errno, "modbus_write_registers ", addr, &retries)) {
            if (!connected) {
                return saved_errno;
            }
            if (--retries > 0) {
                usleep(250); // TODO: Remove this sleep
                continue;
            }
            else {
                return saved_errno;
            }
        }
        else {
            return saved_errno;
        }
    }
    return 0;
}

void ModbusClientThread::operator()() {
    recreateCmdSocket();

    while (!finished) {
        if (!connected) {
            boost::mutex::scoped_lock lock(update_mutex);
            if (ctx) {
                modbus_close(ctx);
                modbus_free(ctx);
                ctx = 0;
            }
            ctx = openConnection();
            if (ctx) {
                connected = true;
                update_status = true;
                // Clear change caches so the first poll after (re)connect republishes
                // current discrete/register state to clockwork (see BufferMonitor::refresh).
                refresh();
            }
            else {
                poll_interval_us =
                    next_modbus_poll_interval_us(false, poll_interval_us, kPollMinUs, kPollMaxUs);
                usleep(poll_interval_us);
                continue;
            }
        }

        performUpdates();
        const bool resync = (slave_silent_cycles > 0) || update_status;
        if (resync) {
            // Slave was missing (or Status left active). Clockwork INPUTBITs were
            // forced off; edge-only publish would skip unchanged 1-bits. Republish.
            refresh();
        }
        if (update_status) {
            sendStatus("initialising");
        }

        bool cycle_ok = true;
        if (!collect_selected_updates(robits_monitor, 1, tab_ro_bits, mc.monitors,
                                      "modbus_read_input_bits", modbus_read_input_bits)) {
            cycle_ok = false;
        }
        if (cycle_ok && !collect_selected_updates(bits_monitor, 0, tab_rp_bits, mc.monitors,
                                                  "modbus_read_bits", modbus_read_bits)) {
            cycle_ok = false;
        }
        if (cycle_ok &&
            !collect_selected_updates(regs_monitor, 3, tab_rq_registers, mc.monitors,
                                      "modbus_read_input registers", modbus_read_input_registers)) {
            cycle_ok = false;
        }
        if (cycle_ok &&
            !collect_selected_updates(holdings_monitor, 4, tab_rw_rq_registers, mc.monitors,
                                      "modbus_read_registers", modbus_read_registers)) {
            cycle_ok = false;
        }

        if (!cycle_ok) {
            if (slave_silent_cycles == 0) {
                sendStatus("disconnected");
                std::cerr << "modbus slave silent — keeping serial open, backing off polls\n";
            }
            ++slave_silent_cycles;
            if (slave_silent_cycles == 1 || (slave_silent_cycles % 50) == 0) {
                std::cerr << "modbus slave still silent (" << slave_silent_cycles << " cycles)\n";
            }
        }
        else {
            if (slave_silent_cycles > 0) {
                std::cerr << "modbus slave returned after " << slave_silent_cycles
                          << " silent cycles — republished\n";
            }
            slave_silent_cycles = 0;
            transient_error_logs = 0;
            if (update_status || resync) {
                sendStatus("active");
                update_status = false;
            }
        }

        poll_interval_us =
            next_modbus_poll_interval_us(cycle_ok, poll_interval_us, kPollMinUs, kPollMaxUs);
        usleep(poll_interval_us);
    }
}

template <typename T>
ModbusClientThread::ChangesOrError
collect_selected_updates(ModbusClientThread &client, modbus_t *ctx, const Options &options,
                         BufferMonitor<T> &bm, unsigned int grp, T *dest,
                         std::map<std::string, ModbusMonitor> &entries, const char *fn_name,
                         int (*read_fn)(modbus_t *ctx, int addr, int nb, T *dest)) {

    ModbusClientThread::ChangesOrError result;
    if (entries.empty()) {
        return result;
    }
    int rc = 0;
    unsigned int min = 100000;
    unsigned int max = 0;
    bool read_failed = false;
    int last_fail = 0;
    std::map<std::string, ModbusMonitor>::const_iterator iter = entries.begin();
    while (iter != entries.end()) {
        const std::pair<std::string, ModbusMonitor> &item = *iter++;
        if (item.second.group() == grp) {
            unsigned int offset = item.second.address();
            unsigned int end = offset + item.second.length() - 1;
            if (offset < min)
                min = offset;
            if (end > max)
                max = end;
            int retry = 2;
            int saved_errno = 0;
            while ((usleep(5000), rc = read_fn(ctx, offset, item.second.length(), dest + offset)) ==
                   -1) {
                saved_errno = errno;
                if (options.verbose)
                    std::cerr << "called: read_fn(ctx, " << offset << ", " << item.second.length()
                              << ", " << dest + offset << "))\n";
                client.check_error(saved_errno, fn_name, static_cast<int>(offset), &retry);
                if (!client.connected) {
                    result.error = saved_errno;
                }
                if (--retry > 0)
                    continue;
                else
                    break;
            }
            if (rc == -1) {
                read_failed = true;
                last_fail = saved_errno ? saved_errno : ETIMEDOUT;
                break;
            }
        }
    }
    if (!client.connected) {
        std::cerr << "Lost connection\n";
        result.error = ENXIO;
        return result;
    }
    if (read_failed) {
        result.error = last_fail;
        return result;
    }
    if (min > max) {
        return result; // No changes to report
    }
    bm.check((max - min + 1), dest + min, (grp << 16) + min, result.changes);
    return result;
}

ModbusClientThread::ChangesOrError ModbusClientThread::collect_bit_changes(
    BufferMonitor<uint8_t> &bm, unsigned int grp, uint8_t *dest,
    std::map<std::string, ModbusMonitor> &entries, const char *fn_name,
    int (*read_fn)(modbus_t *ctx, int addr, int nb, uint8_t *dest)) {
    return ::collect_selected_updates<uint8_t>(*this, ctx, options, bm, grp, dest, entries, fn_name,
                                               read_fn);
}

ModbusClientThread::ChangesOrError ModbusClientThread::collect_word_changes(
    BufferMonitor<uint16_t> &bm, unsigned int grp, uint16_t *dest,
    std::map<std::string, ModbusMonitor> &entries, const char *fn_name,
    int (*read_fn)(modbus_t *ctx, int addr, int nb, uint16_t *dest)) {
    return ::collect_selected_updates<uint16_t>(*this, ctx, options, bm, grp, dest, entries,
                                                fn_name, read_fn);
}

bool ModbusClientThread::collect_selected_updates(
    BufferMonitor<uint8_t> &bm, unsigned int grp, uint8_t *dest,
    std::map<std::string, ModbusMonitor> &entries, const char *fn_name,
    int (*read_fn)(modbus_t *ctx, int addr, int nb, uint8_t *dest)) {
    auto result = ::collect_selected_updates<uint8_t>(*this, ctx, options, bm, grp, dest, entries,
                                                      fn_name, read_fn);
    if (result.error) {
        std::cerr << "Error collecting group " << grp
                  << " updates: " << show_modbus_error(result.error) << "\n";
        return false;
    }
    sendChanges(result.changes, dest, options, [&](ModbusMonitor *mm, bool which) {
        sendStateUpdate(this, mm, which);
    });
    return true;
}

bool ModbusClientThread::collect_selected_updates(
    BufferMonitor<uint16_t> &bm, unsigned int grp, uint16_t *dest,
    std::map<std::string, ModbusMonitor> &entries, const char *fn_name,
    int (*read_fn)(modbus_t *ctx, int addr, int nb, uint16_t *dest)) {
    auto result = ::collect_selected_updates<uint16_t>(*this, ctx, options, bm, grp, dest, entries,
                                                       fn_name, read_fn);
    if (result.error) {
        std::cerr << "Error collecting group " << grp
                  << " updates: " << show_modbus_error(result.error) << "\n";
        return false;
    }
    sendChanges(result.changes, dest, options,
                   [&](ModbusMonitor *mm) { sendPropertyUpdate(this, mm); });
    return true;
}
