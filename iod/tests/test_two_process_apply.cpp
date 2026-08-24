#include "Dispatcher.h"
#include "Logger.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "DbNotify.h"
#include "RecordApply.h"
#include "ThreadSafeQueue.h"
#include "cJSON.h"
#include "library_globals.cpp"
#include <boost/thread/mutex.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <zmq.hpp>

static int run_holder(const char *endpoint, const char *outpath) {
    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    MessageLog::setMaxMemory(10000);
    boost::condition_variable_any cond;
    boost::shared_mutex mutex;
    SharedThreadSafeQueue<Package *> queue(cond, mutex);
    Dispatcher::create(queue);

    MachineClass *mc = new MachineClass("Customer");
    mc->is_record = true;
    mc->table_name = "customer";
    mc->setOption("id", Value(static_cast<int64_t>(0)));
    mc->setColumnFlags("id", MachineClass::COL_KEY);
    mc->setOption("name", Value("", Value::t_string));

    MachineInstance *cust = MachineInstanceFactory::create("cust", "Customer");
    cust->setStateMachine(mc);
    cust->setValue("id", Value(static_cast<int64_t>(1)));
    cust->setValue("name", Value("", Value::t_string));
    machines[cust->getName()] = cust;

    zmq::socket_t sub(*context, ZMQ_SUB);
    int linger = 0;
    sub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    sub.setsockopt(ZMQ_SUBSCRIBE, "", 0);
    sub.connect(endpoint);

    std::string body;
    bool got = false;
    for (int i = 0; i < 40; ++i) {
        zmq::pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        if (zmq::poll(items, 1, 100) > 0 && (items[0].revents & ZMQ_POLLIN)) {
            zmq::message_t note;
            if (sub.recv(&note, 0)) {
                body.assign(static_cast<char *>(note.data()), note.size());
                got = true;
                break;
            }
        }
    }
    if (!got) {
        std::cerr << "holder got no PUB\n";
        return 1;
    }

    std::vector<DbNotifyRow> rows;
    if (parseDbNotify(body, rows) < 1) {
        std::cerr << "holder notify parse failed: " << body << "\n";
        return 2;
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        cJSON *keys = cJSON_Parse(rows[i].keys_json.c_str());
        cJSON *row = cJSON_Parse(rows[i].row_json.c_str());
        RecordApply::applyRow(rows[i].type, keys, row);
        cJSON_Delete(keys);
        cJSON_Delete(row);
    }
    std::string name = cust->getValue("name").asString();
    std::ofstream out(outpath);
    out << name;
    out.close();
    return name == "Ann" ? 0 : 3;
}

static pid_t spawn_holder(const char *self, const char *ep, const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(self, self, "--holder", ep, path, static_cast<char *>(0));
        _exit(127);
    }
    return pid;
}

static int holders_match_ann(pid_t a, pid_t b, const char *path_a, const char *path_b,
                             const char *tag) {
    int status_a = 0, status_b = 0;
    waitpid(a, &status_a, 0);
    waitpid(b, &status_b, 0);
    if (!WIFEXITED(status_a) || WEXITSTATUS(status_a) != 0 || !WIFEXITED(status_b) ||
        WEXITSTATUS(status_b) != 0) {
        std::cerr << tag << " holders failed a=" << status_a << " b=" << status_b << "\n";
        unlink(path_a);
        unlink(path_b);
        return 4;
    }
    std::ifstream in_a(path_a), in_b(path_b);
    std::string name_a, name_b;
    in_a >> name_a;
    in_b >> name_b;
    unlink(path_a);
    unlink(path_b);
    if (name_a != "Ann" || name_b != "Ann") {
        std::cerr << tag << " two processes must match Ann, got '" << name_a << "' and '"
                  << name_b << "'\n";
        return 5;
    }
    return 0;
}

static bool req(zmq::socket_t &s, const std::string &json, std::string &reply, int tries) {
    for (int i = 0; i < tries; ++i) {
        try {
            zmq::message_t m(json.size());
            std::memcpy(m.data(), json.data(), json.size());
            if (!s.send(m, ZMQ_DONTWAIT)) {
                usleep(50000);
                continue;
            }
            zmq::pollitem_t items[] = {{s, 0, ZMQ_POLLIN, 0}};
            if (zmq::poll(items, 1, 500) > 0 && (items[0].revents & ZMQ_POLLIN)) {
                zmq::message_t r;
                if (s.recv(&r, 0)) {
                    reply.assign(static_cast<char *>(r.data()), r.size());
                    return true;
                }
            }
        }
        catch (const zmq::error_t &) {
            usleep(50000);
        }
    }
    return false;
}

static int live_dbsvr_two_holders(const char *self, const char *dbsvr) {
    const int port = 19600 + static_cast<int>(getpid() % 1000);
    const int nport = port + 1;
    char db[128], pbuf[16], nbuf[16], ep[64];
    snprintf(db, sizeof(db), "/tmp/cw_two_apply_%d.db", static_cast<int>(getpid()));
    snprintf(pbuf, sizeof(pbuf), "%d", port);
    snprintf(nbuf, sizeof(nbuf), "%d", nport);
    snprintf(ep, sizeof(ep), "tcp://127.0.0.1:%d", nport);
    unlink(db);
    unlink((std::string(db) + "-wal").c_str());
    unlink((std::string(db) + "-shm").c_str());

    pid_t svr = fork();
    if (svr == 0) {
        execl(dbsvr, dbsvr, "--db", db, "--port", pbuf, "--notify-port", nbuf,
              static_cast<char *>(0));
        _exit(127);
    }

    char path_a[128], path_b[128];
    snprintf(path_a, sizeof(path_a), "/tmp/cw_two_apply_%d_la", static_cast<int>(getpid()));
    snprintf(path_b, sizeof(path_b), "/tmp/cw_two_apply_%d_lb", static_cast<int>(getpid()));
    unlink(path_a);
    unlink(path_b);
    usleep(200000);
    pid_t a = spawn_holder(self, ep, path_a);
    pid_t b = spawn_holder(self, ep, path_b);
    usleep(800000);

    zmq::context_t ctx;
    zmq::socket_t client(ctx, ZMQ_REQ);
    int linger = 0;
    client.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    char reqep[64];
    snprintf(reqep, sizeof(reqep), "tcp://127.0.0.1:%d", port);
    client.connect(reqep);

    std::string reply;
    const char *create =
        "{\"action\":\"create\",\"auth\":\"xxx\",\"type\":\"customer\","
        "\"schema\":{\"id\":\"integer primary key\",\"name\":\"text\"}}";
    const char *ins =
        "{\"action\":\"insert\",\"auth\":\"xxx\",\"type\":\"customer\","
        "\"data\":{\"id\":1,\"name\":\"Ann\"}}";
    if (!req(client, create, reply, 40) || !req(client, ins, reply, 10) ||
        reply.find("Ann") == std::string::npos) {
        std::cerr << "live dbsvr insert failed: " << reply << "\n";
        kill(a, SIGTERM);
        kill(b, SIGTERM);
        kill(svr, SIGTERM);
        waitpid(a, 0, 0);
        waitpid(b, 0, 0);
        waitpid(svr, 0, 0);
        return 6;
    }

    int rc = holders_match_ann(a, b, path_a, path_b, "live dbsvr");
    kill(svr, SIGTERM);
    waitpid(svr, 0, 0);
    unlink(db);
    unlink((std::string(db) + "-wal").c_str());
    unlink((std::string(db) + "-shm").c_str());
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 4 && std::strcmp(argv[1], "--holder") == 0) {
        return run_holder(argv[2], argv[3]);
    }

    const int port = 18600 + static_cast<int>(getpid() % 1000);
    char ep[64];
    snprintf(ep, sizeof(ep), "tcp://127.0.0.1:%d", port);
    char path_a[128], path_b[128];
    snprintf(path_a, sizeof(path_a), "/tmp/cw_two_apply_%d_a", static_cast<int>(getpid()));
    snprintf(path_b, sizeof(path_b), "/tmp/cw_two_apply_%d_b", static_cast<int>(getpid()));
    unlink(path_a);
    unlink(path_b);

    pid_t a = spawn_holder(argv[0], ep, path_a);
    pid_t b = spawn_holder(argv[0], ep, path_b);

    zmq::context_t ctx;
    zmq::socket_t pub(ctx, ZMQ_PUB);
    int linger = 0;
    pub.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    pub.bind(ep);
    usleep(800000);

    const char *payload =
        "{\"action\":\"insert\",\"type\":\"customer\",\"keys\":{\"id\":1},"
        "\"row\":{\"id\":1,\"name\":\"Ann\"}}";
    zmq::message_t m(std::strlen(payload));
    std::memcpy(m.data(), payload, std::strlen(payload));
    pub.send(m, 0);

    int rc = holders_match_ann(a, b, path_a, path_b, "local PUB");
    if (rc != 0) {
        return rc;
    }

    const char *dbsvr = getenv("DBSVR");
    if (dbsvr && *dbsvr) {
        rc = live_dbsvr_two_holders(argv[0], dbsvr);
        if (rc != 0) {
            return rc;
        }
    }
    std::cout << "ok\n";
    return 0;
}
