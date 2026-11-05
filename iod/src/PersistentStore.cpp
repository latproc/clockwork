#include "PersistentStore.h"
#include "value.h"
#include <boost/foreach.hpp>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <streambuf>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

const char *escaped_string_prefix = "@LATPROC-ESC1:";

void set_error(std::string *error, const std::string &message) {
    if (error) {
        *error = message;
    }
}

std::string system_error(const std::string &operation, const std::string &path) {
    std::ostringstream out;
    out << operation << " " << path << ": " << strerror(errno);
    return out.str();
}

std::string parent_directory(const std::string &path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

bool path_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool same_file_state(const struct stat &left, const struct stat &right) {
    if (left.st_dev != right.st_dev || left.st_ino != right.st_ino ||
        left.st_size != right.st_size || left.st_mtime != right.st_mtime) {
        return false;
    }
#if defined(__APPLE__)
    return left.st_mtimespec.tv_nsec == right.st_mtimespec.tv_nsec;
#elif defined(__linux__)
    return left.st_mtim.tv_nsec == right.st_mtim.tv_nsec;
#else
    return true;
#endif
}

bool retry_fsync(int fd) {
    int rc;
    do {
        rc = fsync(fd);
    } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

bool sync_directory(const std::string &directory, std::string *error) {
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int fd = open(directory.c_str(), flags);
    if (fd < 0) {
        set_error(error, system_error("open directory", directory));
        return false;
    }
    bool ok = retry_fsync(fd);
    int saved_errno = errno;
    close(fd);
    if (!ok) {
        // Directory fsync is required for the Linux durability guarantee.
        // Some local development filesystems reject it.
        if (saved_errno == EINVAL || saved_errno == ENOSYS
#ifdef ENOTSUP
            || saved_errno == ENOTSUP
#endif
        ) {
            return true;
        }
        errno = saved_errno;
        set_error(error, system_error("fsync directory", directory));
    }
    return ok;
}

bool valid_persistent_key(const std::string &key) {
    if (key.empty()) {
        return false;
    }
    for (size_t i = 0; i < key.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool safe_symbol(const std::string &value) {
    if (value.empty() || !std::isalpha(static_cast<unsigned char>(value[0]))) {
        return false;
    }
    for (size_t i = 1; i < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(c) && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

bool needs_escaped_encoding(const std::string &value) {
    if (value.compare(0, strlen(escaped_string_prefix), escaped_string_prefix) == 0) {
        return true;
    }
    if (!value.empty() && value[0] == '"' && value[value.size() - 1] == '"') {
        return true;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
            return true;
        }
    }
    return false;
}

std::string escape_string(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        switch (value[i]) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += value[i];
            break;
        }
    }
    return escaped;
}

bool unescape_string(const std::string &encoded, std::string *value) {
    value->clear();
    value->reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '\\') {
            *value += encoded[i];
            continue;
        }
        if (++i >= encoded.size()) {
            return false;
        }
        switch (encoded[i]) {
        case '\\':
            *value += '\\';
            break;
        case '"':
            *value += '"';
            break;
        case 'n':
            *value += '\n';
            break;
        case 'r':
            *value += '\r';
            break;
        case 't':
            *value += '\t';
            break;
        default:
            return false;
        }
    }
    return true;
}

std::string quote_string(const std::string &value) {
    if (needs_escaped_encoding(value)) {
        return std::string("\"") + escaped_string_prefix + escape_string(value) + "\"";
    }
    return std::string("\"") + value + "\"";
}

std::string serialise_value(const Value &value) {
    std::ostringstream out;
    switch (value.kind) {
    case Value::t_bool:
        return value.asString() == "true" || value.asString() == "TRUE" ? "TRUE" : "FALSE";
    case Value::t_integer:
        out << value.asString();
        return out.str();
    case Value::t_float:
        out << std::setprecision(std::numeric_limits<double>::max_digits10)
            << std::scientific << value.toFloat();
        return out.str();
    case Value::t_string:
        return quote_string(value.asString());
    case Value::t_symbol: {
        std::string text = value.asString();
        if (safe_symbol(text) && text != "TRUE" && text != "FALSE" && text != "true" &&
            text != "false" && text != "NULL") {
            char *end = 0;
            errno = 0;
            strtod(text.c_str(), &end);
            if (!(end && *end == 0)) {
                return text;
            }
        }
        return quote_string(text);
    }
    case Value::t_empty:
        return "NULL";
    case Value::t_json:
    case Value::t_dynamic:
        return quote_string(value.asString());
    }
    return quote_string(value.asString());
}

bool parse_value(const std::string &text, Value *value, std::string *error) {
    if (text.size() >= 2 && text[0] == '"' && text[text.size() - 1] == '"') {
        std::string inner = text.substr(1, text.size() - 2);
        const size_t prefix_len = strlen(escaped_string_prefix);
        if (inner.compare(0, prefix_len, escaped_string_prefix) == 0) {
            std::string decoded;
            if (!unescape_string(inner.substr(prefix_len), &decoded)) {
                set_error(error, "invalid escape sequence in persisted string");
                return false;
            }
            *value = Value(decoded, Value::t_string);
        }
        else {
            *value = Value(inner, Value::t_string);
        }
        return true;
    }
    if (text == "NULL") {
        *value = Value();
        return true;
    }
    if (text == "TRUE" || text == "true" || text == "FALSE" || text == "false") {
        *value = Value(text == "TRUE" || text == "true");
        return true;
    }

    char *end = 0;
    errno = 0;
    long long integer = strtoll(text.c_str(), &end, 10);
    if (errno == 0 && end && *end == 0) {
        *value = Value(static_cast<int64_t>(integer));
        return true;
    }

    errno = 0;
    end = 0;
    double real = strtod(text.c_str(), &end);
    if (errno == 0 && end && *end == 0) {
        *value = Value(real);
        return true;
    }

    *value = Value(text, Value::t_symbol);
    return true;
}

bool parse_store_line(const std::string &line, std::string *machine, std::string *property,
                      Value *value, std::string *error) {
    size_t pos = 0;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    size_t machine_start = pos;
    while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos == machine_start) {
        set_error(error, "missing machine name");
        return false;
    }
    *machine = line.substr(machine_start, pos - machine_start);

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    size_t property_start = pos;
    while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos == property_start) {
        set_error(error, "missing property name");
        return false;
    }
    *property = line.substr(property_start, pos - property_start);

    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos == line.size()) {
        set_error(error, "missing property value");
        return false;
    }
    return parse_value(line.substr(pos), value, error);
}

class FdStreambuf : public std::streambuf {
  public:
    explicit FdStreambuf(int fd) : fd_(fd), bytes_written_(0), failed_(false) {}

    bool failed() const { return failed_; }

  protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::not_eof(ch);
        }
        char byte = static_cast<char>(ch);
        if (!write_bytes(&byte, 1)) {
            return traits_type::eof();
        }
        return ch;
    }

    std::streamsize xsputn(const char *data, std::streamsize length) override {
        if (length <= 0) {
            return 0;
        }
        return write_bytes(data, static_cast<size_t>(length)) ? length : 0;
    }

  private:
    bool raw_write(const char *data, size_t length) {
        while (length) {
            ssize_t written = write(fd_, data, length);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                failed_ = true;
                return false;
            }
            if (written == 0) {
                errno = EIO;
                failed_ = true;
                return false;
            }
            data += written;
            length -= static_cast<size_t>(written);
            bytes_written_ += static_cast<uint64_t>(written);
        }
        return true;
    }

    bool write_bytes(const char *data, size_t length) {
#ifdef PERSISTENT_STORE_TESTING
        if (const char *configured = getenv("PERSISTENT_STORE_TEST_FAIL_AFTER_BYTES")) {
            char *end = 0;
            unsigned long long fail_after = strtoull(configured, &end, 10);
            if (end && *end == 0 && fail_after != 0 &&
                bytes_written_ + length > fail_after) {
                size_t permitted =
                    fail_after > bytes_written_ ? fail_after - bytes_written_ : 0;
                if (permitted && !raw_write(data, permitted)) {
                    return false;
                }
                errno = ENOSPC;
                failed_ = true;
                return false;
            }
        }
#endif
        return raw_write(data, length);
    }

    int fd_;
    uint64_t bytes_written_;
    bool failed_;
};

#ifdef PERSISTENT_STORE_TESTING
bool test_fault(const char *name) {
    const char *configured = getenv("PERSISTENT_STORE_TEST_FAULT");
    return configured && strcmp(configured, name) == 0;
}
#else
bool test_fault(const char *name) {
    (void)name;
    return false;
}
#endif

} // namespace

void PersistentStore::insert(std::string machine, std::string key, Value value) {
    std::map<std::string, std::map<std::string, Value>>::iterator found = init_values.find(machine);
    if (found == init_values.end()) {
        std::map<std::string, Value> entry;
        entry[key] = value;
        init_values[machine] = entry;
    }
    else {
        std::map<std::string, Value> &entry = (*found).second;
        entry[key] = value;
    }
    is_dirty = true;
}

bool PersistentStore::load_file(
    const std::string &path, bool &missing, std::string *error,
    std::map<std::string, std::map<std::string, Value>> &values) const {
    missing = false;
    values.clear();

    std::ifstream store(path.c_str(), std::ios::in | std::ios::binary);
    if (!store.is_open()) {
        int saved_errno = errno;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 && errno == ENOENT) {
            missing = true;
            return true;
        }
        errno = saved_errno;
        set_error(error, system_error("open store", path));
        return false;
    }

    store.seekg(0, std::ios::end);
    std::streamoff size = store.tellg();
    if (size < 0) {
        set_error(error, "seek failed in " + path);
        return false;
    }
    if (size == 0) {
        missing = true;
        return true;
    }
    store.seekg(size - 1);
    char last = 0;
    store.get(last);
    if (!store || last != '\n') {
        set_error(error, "store does not end with a complete record: " + path);
        return false;
    }
    store.clear();
    store.seekg(0, std::ios::beg);

    std::string line;
    size_t line_number = 0;
    while (std::getline(store, line)) {
        ++line_number;
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        if (line.empty()) {
            continue;
        }
        std::string name;
        std::string property;
        Value value;
        std::string line_error;
        if (!parse_store_line(line, &name, &property, &value, &line_error)) {
            std::ostringstream message;
            message << path << ":" << line_number << ": " << line_error;
            set_error(error, message.str());
            values.clear();
            return false;
        }
        values[name][property] = value;
    }
    if (!store.eof()) {
        set_error(error, "read failed in " + path);
        values.clear();
        return false;
    }
    return true;
}

bool PersistentStore::validate_file(const std::string &path, std::string *error) const {
    bool missing = false;
    std::map<std::string, std::map<std::string, Value>> ignored;
    return load_file(path, missing, error, ignored) && !missing;
}

bool PersistentStore::load(std::string *error) {
    if (error) {
        error->clear();
    }

    std::map<std::string, std::map<std::string, Value>> loaded;
    bool missing = false;
    std::string primary_error;
    if (load_file(file_name, missing, &primary_error, loaded)) {
        if (!missing) {
            init_values.swap(loaded);
            is_dirty = false;
            primary_valid = true;
            primary_stat_valid = stat(file_name.c_str(), &primary_stat) == 0;
            if (!primary_stat_valid) {
                primary_valid = false;
            }
            return true;
        }
    }

    primary_valid = false;
    primary_stat_valid = false;
    std::string backup_name = file_name + ".bak";
    bool backup_missing = false;
    std::string backup_error;
    if (load_file(backup_name, backup_missing, &backup_error, loaded) && !backup_missing) {
        init_values.swap(loaded);
        is_dirty = false;
        const std::string reason =
            primary_error.empty() ? "primary store missing or empty" : primary_error;
        std::cerr << "PersistentStore: " << reason << "; recovered from " << backup_name << "\n";
        if (error) {
            *error = reason + "; recovered from " + backup_name;
        }
        return true;
    }

    init_values.clear();
    is_dirty = false;
    if (missing && backup_missing) {
        return true;
    }
    std::ostringstream message;
    if (!primary_error.empty()) {
        message << primary_error;
    }
    if (!backup_error.empty()) {
        if (message.tellp() > 0) {
            message << "; ";
        }
        message << backup_error;
    }
    if (message.tellp() == 0) {
        message << "no readable persistent data in " << file_name;
    }
    set_error(error, message.str());
    return false;
}

void PersistentStore::split(std::string &name, std::string &prop) const {
    name = prop;
    size_t pos = name.rfind('.');
    if (pos == std::string::npos) {
        name.clear();
        return;
    }
    name.erase(pos);
    prop = prop.substr(pos + 1);
}

std::ostream &PersistentStore::operator<<(std::ostream &out) const {
    std::pair<std::string, std::map<std::string, Value>> prop;
    BOOST_FOREACH (prop, init_values) {
        std::map<std::string, Value> &entries(prop.second);
        PersistentStore::PropertyPair entry;
        BOOST_FOREACH (entry, entries) {
            out << prop.first << " " << entry.first << " " << serialise_value(entry.second)
                << "\n";
        }
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const PersistentStore &store) {
    return store.operator<<(out);
}

bool PersistentStore::save(std::string *error) {
    if (error) {
        error->clear();
    }

    std::map<std::string, std::map<std::string, Value>>::const_iterator machine =
        init_values.begin();
    while (machine != init_values.end()) {
        if (!valid_persistent_key(machine->first)) {
            set_error(error, "invalid machine name in persistent store: " + machine->first);
            return false;
        }
        std::map<std::string, Value>::const_iterator property = machine->second.begin();
        while (property != machine->second.end()) {
            if (!valid_persistent_key(property->first)) {
                set_error(error, "invalid property name in persistent store: " + property->first);
                return false;
            }
            ++property;
        }
        ++machine;
    }

    std::string scratchfile = file_name + ".tmp.XXXXXX";
    std::vector<char> scratch_template(scratchfile.begin(), scratchfile.end());
    scratch_template.push_back('\0');
    int fd = mkstemp(&scratch_template[0]);
    if (fd < 0) {
        set_error(error, system_error("create temporary store", scratchfile));
        return false;
    }
    scratchfile.assign(&scratch_template[0]);

    struct stat existing;
    mode_t mode = 0644;
    if (stat(file_name.c_str(), &existing) == 0) {
        mode = existing.st_mode & 0777;
    }
    if (fchmod(fd, mode) != 0) {
        set_error(error, system_error("set permissions on", scratchfile));
        close(fd);
        unlink(scratchfile.c_str());
        return false;
    }

    bool write_ok = false;
    std::string write_error;
    {
        FdStreambuf buffer(fd);
        std::ostream out(&buffer);
        try {
            out << *this;
            out.flush();
            write_ok = out.good() && !buffer.failed();
            if (!write_ok) {
                write_error = system_error("write", scratchfile);
            }
        }
        catch (const std::exception &e) {
            write_error = std::string("write ") + scratchfile + ": " + e.what();
        }
        catch (...) {
            write_error = "unknown exception writing " + scratchfile;
        }
    }
    if (!write_ok) {
        set_error(error, write_error);
        close(fd);
        unlink(scratchfile.c_str());
        return false;
    }

    if (test_fault("fsync_temp") || !retry_fsync(fd)) {
        set_error(error, test_fault("fsync_temp") ? "injected fsync_temp failure"
                                                  : system_error("fsync", scratchfile));
        close(fd);
        unlink(scratchfile.c_str());
        return false;
    }
    if (close(fd) != 0) {
        set_error(error, system_error("close", scratchfile));
        unlink(scratchfile.c_str());
        return false;
    }

    const std::string directory = parent_directory(file_name);
    const bool main_exists = path_exists(file_name);
    struct stat current_main;
    if (main_exists && stat(file_name.c_str(), &current_main) != 0) {
        set_error(error, system_error("stat", file_name));
        unlink(scratchfile.c_str());
        return false;
    }
    const bool main_changed =
        primary_stat_valid ? !same_file_state(primary_stat, current_main) : true;
    if (main_exists && (!primary_valid || main_changed)) {
        std::string validation_error;
        primary_valid = validate_file(file_name, &validation_error);
    }

    if (main_exists && primary_valid) {
        const std::string backup = file_name + ".bak";
        std::ostringstream backup_tmp_name;
        backup_tmp_name << backup << ".tmp." << static_cast<long long>(getpid());
        const std::string backup_tmp = backup_tmp_name.str();
        unlink(backup_tmp.c_str());
        if (link(file_name.c_str(), backup_tmp.c_str()) != 0) {
            set_error(error, system_error("link backup", backup_tmp));
            unlink(scratchfile.c_str());
            return false;
        }
        if (test_fault("rename_backup") ||
            rename(backup_tmp.c_str(), backup.c_str()) != 0) {
            set_error(error, test_fault("rename_backup") ? "injected rename_backup failure"
                                                         : system_error("rename backup", backup));
            unlink(backup_tmp.c_str());
            unlink(scratchfile.c_str());
            return false;
        }
        if (test_fault("fsync_backup") || !sync_directory(directory, error)) {
            if (test_fault("fsync_backup")) {
                set_error(error, "injected fsync_backup failure");
            }
            unlink(scratchfile.c_str());
            return false;
        }
    }

    if (test_fault("rename_main") || rename(scratchfile.c_str(), file_name.c_str()) != 0) {
        set_error(error, test_fault("rename_main") ? "injected rename_main failure"
                                                   : system_error("rename", file_name));
        unlink(scratchfile.c_str());
        return false;
    }

    if (test_fault("fsync_main") || !sync_directory(directory, error)) {
        if (test_fault("fsync_main")) {
            set_error(error, "injected fsync_main failure");
        }
        return false;
    }

    is_dirty = false;
    primary_valid = true;
    primary_stat_valid = stat(file_name.c_str(), &primary_stat) == 0;
    return true;
}
