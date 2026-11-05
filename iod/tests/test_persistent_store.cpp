#include "PersistentStore.h"
#include "gtest/gtest.h"

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

class TempDirectory {
  public:
    TempDirectory() {
        char path[] = "/tmp/persist-store-test.XXXXXX";
        char *made = mkdtemp(path);
        if (made) {
            path_ = made;
        }
    }

    ~TempDirectory() {
        if (path_.empty()) {
            return;
        }
        DIR *dir = opendir(path_.c_str());
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != 0) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    unlink((path_ + "/" + entry->d_name).c_str());
                }
            }
            closedir(dir);
        }
        rmdir(path_.c_str());
    }

    const std::string &path() const { return path_; }
    std::string file(const std::string &name = "persist.dat") const {
        return path_ + "/" + name;
    }

  private:
    std::string path_;
};

std::string read_file(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write_file(const std::string &path, const std::string &data, bool sync_file = false) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    size_t offset = 0;
    while (offset < data.size()) {
        ssize_t written = write(fd, data.data() + offset, data.size() - offset);
        ASSERT_GT(written, 0);
        offset += static_cast<size_t>(written);
    }
    if (sync_file) {
        ASSERT_EQ(fsync(fd), 0);
    }
    ASSERT_EQ(close(fd), 0);
}

void clear_faults() {
    unsetenv("PERSISTENT_STORE_TEST_FAULT");
    unsetenv("PERSISTENT_STORE_TEST_FAIL_AFTER_BYTES");
}

void expect_equivalent(const std::map<std::string, std::map<std::string, Value>> &expected,
                       const std::map<std::string, std::map<std::string, Value>> &actual) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::map<std::string, std::map<std::string, Value>>::const_iterator machine =
             expected.begin();
         machine != expected.end(); ++machine) {
        ASSERT_EQ(actual.count(machine->first), 1u) << machine->first;
        const std::map<std::string, Value> &actual_properties = actual.at(machine->first);
        ASSERT_EQ(actual_properties.size(), machine->second.size()) << machine->first;
        for (std::map<std::string, Value>::const_iterator property = machine->second.begin();
             property != machine->second.end(); ++property) {
            ASSERT_EQ(actual_properties.count(property->first), 1u)
                << machine->first << "." << property->first;
            const Value &expected_value = property->second;
            const Value &actual_value = actual_properties.at(property->first);
            EXPECT_EQ(actual_value.kind, expected_value.kind)
                << machine->first << "." << property->first;
            if (expected_value.kind == Value::t_float) {
                EXPECT_TRUE(actual_value.identical(expected_value))
                    << machine->first << "." << property->first;
            }
            else {
                EXPECT_EQ(actual_value.asString(), expected_value.asString())
                    << machine->first << "." << property->first;
            }
        }
    }
}

void put_generation(PersistentStore &store, unsigned generation, size_t payload_size) {
    std::ostringstream generation_value;
    generation_value << "generation-" << generation;
    store.insert("M_TEST", "generation", Value(generation_value.str(), Value::t_string));
    for (size_t i = 0; i < 128; ++i) {
        std::ostringstream key;
        key << "payload_" << i;
        std::string value = generation_value.str() + ":";
        value.append(payload_size, static_cast<char>('a' + (i % 26)));
        store.insert("M_TEST", key.str(), Value(value, Value::t_string));
    }
}

void expect_generation(PersistentStore &store, unsigned expected) {
    ASSERT_EQ(store.init_values.count("M_TEST"), 1u);
    const std::map<std::string, Value> &properties = store.init_values["M_TEST"];
    ASSERT_EQ(properties.count("generation"), 1u);
    std::ostringstream generation_value;
    generation_value << "generation-" << expected;
    EXPECT_EQ(properties.at("generation").asString(), generation_value.str());
    for (size_t i = 0; i < 128; ++i) {
        std::ostringstream key;
        key << "payload_" << i;
        ASSERT_EQ(properties.count(key.str()), 1u);
        EXPECT_EQ(properties.at(key.str()).asString().compare(0, generation_value.str().size(),
                                                              generation_value.str()),
                  0);
    }
}

bool read_generation(const std::string &path, PersistentStore &store, unsigned &generation) {
    std::string error;
    if (!store.load(&error)) {
        return false;
    }
    if (!store.init_values.count("M_TEST") ||
        !store.init_values["M_TEST"].count("generation")) {
        return false;
    }
    std::string value = store.init_values["M_TEST"]["generation"].asString();
    const char *number = value.c_str() + strlen("generation-");
    char *end = 0;
    unsigned long parsed = strtoul(number, &end, 10);
    if (!end || *end != 0) {
        return false;
    }
    generation = static_cast<unsigned>(parsed);
    for (size_t i = 0; i < 128; ++i) {
        std::ostringstream key;
        key << "payload_" << i;
        if (!store.init_values["M_TEST"].count(key.str()) ||
            store.init_values["M_TEST"][key.str()].asString().compare(
                0, value.size(), value) != 0) {
            return false;
        }
    }
    return true;
}

void expect_no_temporary_files(const TempDirectory &directory) {
    DIR *dir = opendir(directory.path().c_str());
    ASSERT_NE(dir, nullptr);
    struct dirent *entry;
    while ((entry = readdir(dir)) != 0) {
        std::string name = entry->d_name;
        EXPECT_EQ(name.find(".tmp."), std::string::npos) << name;
    }
    closedir(dir);
}

void populate_types(PersistentStore &store) {
    store.insert("M_TYPES", "bool_true", Value(true));
    store.insert("M_TYPES", "bool_false", Value(false));
    store.insert("M_TYPES", "integer", Value(static_cast<int64_t>(-9223372036854775000LL)));
    store.insert("M_TYPES", "real", Value(0.12345678901234567));
    store.insert("M_TYPES", "symbol", Value("FastMove", Value::t_symbol));
    store.insert("M_TYPES", "string", Value("a value with spaces", Value::t_string));
    store.insert("M_TYPES", "quoted", Value("\"quoted\" and \\slashed\\", Value::t_string));
    store.insert("M_TYPES", "multiline",
                 Value("line one\nline two\ttabbed\rreturn", Value::t_string));
    store.insert("M_TYPES", "prefix_literal", Value("@LATPROC-ESC1:literal", Value::t_string));
    store.insert("M_TYPES", "empty", Value());
}

std::string line_for(const PersistentStore &store, const std::string &machine,
                     const std::string &property) {
    std::ostringstream out;
    store.operator<<(out);
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string actual_machine;
        std::string actual_property;
        fields >> actual_machine >> actual_property;
        if (actual_machine == machine && actual_property == property) {
            return line;
        }
    }
    return "";
}

} // namespace

TEST(PersistentStore, RoundTripsLargeDatasetAndValueTypes) {
    TempDirectory directory;
    ASSERT_FALSE(directory.path().empty());
    PersistentStore store(directory.file());
    populate_types(store);
    for (size_t machine = 0; machine < 2500; ++machine) {
        std::ostringstream name;
        name << "M_" << machine;
        for (size_t property = 0; property < 8; ++property) {
            std::ostringstream key;
            key << "P_" << property;
            std::ostringstream value;
            value << name.str() << ":" << key.str();
            store.insert(name.str(), key.str(), Value(value.str(), Value::t_string));
        }
    }
    std::string large(1024 * 1024, 'x');
    store.insert("M_LARGE", "one_megabyte_string", Value(large, Value::t_string));

    std::string error;
    ASSERT_TRUE(store.save(&error)) << error;
    ASSERT_FALSE(store.dirty());

    PersistentStore loaded(directory.file());
    ASSERT_TRUE(loaded.load(&error)) << error;
    expect_equivalent(store.init_values, loaded.init_values);

    PersistentStore rewritten(directory.file("rewritten.dat"));
    rewritten.init_values = loaded.init_values;
    ASSERT_TRUE(rewritten.save(&error)) << error;
    EXPECT_EQ(read_file(directory.file("rewritten.dat")), read_file(directory.file()));
}

TEST(PersistentStore, KeepsLegacyFormatWhereItIsLossless) {
    TempDirectory directory;
    PersistentStore store(directory.file());
    store.insert("M_FORMAT", "zero", Value(0.0));
    store.insert("M_FORMAT", "simple", Value(0.05));
    store.insert("M_FORMAT", "ulp_noise", Value(std::nextafter(0.05, 0.0)));
    store.insert("M_FORMAT", "precise", Value(0.12345678901234567));
    store.insert("M_FORMAT", "enabled", Value(true));

    EXPECT_EQ(line_for(store, "M_FORMAT", "zero"), "M_FORMAT zero 0.0");
    EXPECT_EQ(line_for(store, "M_FORMAT", "simple"), "M_FORMAT simple 0.050000");
    EXPECT_EQ(line_for(store, "M_FORMAT", "ulp_noise"), "M_FORMAT ulp_noise 0.050000");
    EXPECT_NE(line_for(store, "M_FORMAT", "precise"), "M_FORMAT precise 0.123457");
    EXPECT_EQ(line_for(store, "M_FORMAT", "enabled"), "M_FORMAT enabled true");
}

TEST(PersistentStore, KeepsBackupAndRecoversCorruptPrimary) {
    TempDirectory directory;
    PersistentStore store(directory.file());
    put_generation(store, 1, 64);
    std::string error;
    ASSERT_TRUE(store.save(&error)) << error;
    put_generation(store, 2, 64);
    ASSERT_TRUE(store.save(&error)) << error;

    write_file(directory.file(), "M_TEST generation \"truncated", false);
    PersistentStore recovered(directory.file());
    ASSERT_TRUE(recovered.load(&error)) << error;
    expect_generation(recovered, 1);

    store.insert("M_TEST", "after_recovery", Value("complete", Value::t_string));
    ASSERT_TRUE(store.save(&error)) << error;
    PersistentStore after(directory.file());
    ASSERT_TRUE(after.load(&error)) << error;
    EXPECT_EQ(after.init_values["M_TEST"]["after_recovery"].asString(), "complete");
    PersistentStore backup(directory.file() + ".bak");
    ASSERT_TRUE(backup.load(&error)) << error;
    expect_generation(backup, 1);
}

TEST(PersistentStore, RejectsPartialOrFailedWritesWithoutReplacingPrimary) {
    TempDirectory directory;
    PersistentStore store(directory.file());
    put_generation(store, 10, 256);
    std::string error;
    ASSERT_TRUE(store.save(&error)) << error;
    std::string before = read_file(directory.file());

    setenv("PERSISTENT_STORE_TEST_FAIL_AFTER_BYTES", "97", 1);
    put_generation(store, 11, 256);
    ASSERT_FALSE(store.save(&error));
    clear_faults();
    EXPECT_EQ(read_file(directory.file()), before);
    expect_no_temporary_files(directory);

    setenv("PERSISTENT_STORE_TEST_FAULT", "fsync_temp", 1);
    put_generation(store, 12, 256);
    ASSERT_FALSE(store.save(&error));
    clear_faults();
    EXPECT_EQ(read_file(directory.file()), before);
    expect_no_temporary_files(directory);

    ASSERT_TRUE(store.save(&error)) << error;
    PersistentStore retried(directory.file());
    ASSERT_TRUE(retried.load(&error)) << error;
    expect_generation(retried, 12);
}

TEST(PersistentStore, EveryInjectedStorageFailureKeepsAParseableGeneration) {
    const char *faults[] = {"fsync_temp", "rename_backup", "fsync_backup", "rename_main",
                            "fsync_main"};
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        TempDirectory directory;
        PersistentStore store(directory.file());
        put_generation(store, 20, 256);
        std::string error;
        ASSERT_TRUE(store.save(&error)) << error;

        put_generation(store, 21, 256);
        setenv("PERSISTENT_STORE_TEST_FAULT", faults[i], 1);
        EXPECT_FALSE(store.save(&error)) << faults[i];
        clear_faults();

        PersistentStore recovered(directory.file());
        unsigned generation = 0;
        ASSERT_TRUE(read_generation(directory.file(), recovered, generation)) << faults[i];
        EXPECT_TRUE(generation == 20 || generation == 21) << faults[i];
        expect_no_temporary_files(directory);
    }
}

TEST(PersistentStore, SurvivesSigkillAndSigtermDuringRepeatedLargeWrites) {
    const int signals[] = {SIGKILL, SIGTERM};
    unsigned seed = 0x12345678;
    for (size_t signal_index = 0; signal_index < 2; ++signal_index) {
        for (int run = 0; run < 20; ++run) {
            TempDirectory directory;
            PersistentStore initial(directory.file());
            put_generation(initial, 0, 2048);
            std::string error;
            ASSERT_TRUE(initial.save(&error)) << error;

            pid_t child = fork();
            ASSERT_GE(child, 0);
            if (child == 0) {
                PersistentStore writer(directory.file());
                for (unsigned generation = 1;; ++generation) {
                    put_generation(writer, generation, 2048);
                    if (!writer.save()) {
                        _exit(2);
                    }
                }
            }

            seed = seed * 1103515245u + 12345u;
            useconds_t delay = 500 + (seed % 20000);
            usleep(delay);
            kill(child, signals[signal_index]);
            int status = 0;
            ASSERT_EQ(waitpid(child, &status, 0), child);

            PersistentStore recovered(directory.file());
            unsigned generation = 0;
            ASSERT_TRUE(read_generation(directory.file(), recovered, generation))
                << "run=" << run << " signal=" << signals[signal_index];
        }
    }
}

TEST(PersistentStore, RejectsNamesThatCannotRoundTrip) {
    TempDirectory directory;
    PersistentStore store(directory.file());
    store.insert("machine with space", "valid", Value(1));
    std::string error;
    EXPECT_FALSE(store.save(&error));
    EXPECT_NE(error.find("invalid machine name"), std::string::npos);
    EXPECT_NE(::access(directory.file().c_str(), F_OK), 0);
}

TEST(PersistentStore, MissingStoreIsEmptyButTruncatedStoreIsRejected) {
    TempDirectory directory;
    PersistentStore missing(directory.file());
    std::string error;
    EXPECT_TRUE(missing.load(&error));
    EXPECT_TRUE(missing.init_values.empty());

    write_file(directory.file(), "M_TEST generation 1", false);
    PersistentStore truncated(directory.file());
    EXPECT_FALSE(truncated.load(&error));
    EXPECT_NE(error.find("complete record"), std::string::npos);
}

TEST(PersistentStore, EmptyPrimaryDoesNotHideACompleteBackup) {
    TempDirectory directory;
    PersistentStore store(directory.file());
    put_generation(store, 30, 64);
    std::string error;
    ASSERT_TRUE(store.save(&error)) << error;
    put_generation(store, 31, 64);
    ASSERT_TRUE(store.save(&error)) << error;
    write_file(directory.file(), "", false);

    PersistentStore recovered(directory.file());
    ASSERT_TRUE(recovered.load(&error)) << error;
    expect_generation(recovered, 30);
}

TEST(PersistentStore, OptionalExternalProductionFixtureRoundTripsExactly) {
    const char *fixture = getenv("PERSIST_STORE_EXTERNAL_FIXTURE");
    if (!fixture || !*fixture) {
        GTEST_SKIP() << "set PERSIST_STORE_EXTERNAL_FIXTURE to a real persist.dat";
    }
    TempDirectory directory;
    PersistentStore original(fixture);
    std::string error;
    ASSERT_TRUE(original.load(&error)) << error;
    ASSERT_FALSE(original.init_values.empty());

    PersistentStore round_tripped(directory.file());
    round_tripped.init_values = original.init_values;
    ASSERT_TRUE(round_tripped.save(&error)) << error;

    PersistentStore reloaded(directory.file());
    ASSERT_TRUE(reloaded.load(&error)) << error;
    expect_equivalent(original.init_values, reloaded.init_values);

    PersistentStore rewritten(directory.file("rewritten.dat"));
    rewritten.init_values = reloaded.init_values;
    ASSERT_TRUE(rewritten.save(&error)) << error;
    EXPECT_EQ(read_file(directory.file("rewritten.dat")), read_file(directory.file()));
}
