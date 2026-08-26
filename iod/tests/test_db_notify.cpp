#include "DbNotify.h"
#include <iostream>
#include <string>

int main() {
    std::vector<DbNotifyRow> rows;
    const char *one = "{\"action\":\"insert\",\"type\":\"customer\",\"keys\":{\"id\":1},"
                      "\"row\":{\"id\":1,\"name\":\"Ann\"}}";
    if (parseDbNotify(one, rows) != 1 || rows[0].type != "customer" ||
        rows[0].row_json.find("Ann") == std::string::npos) {
        std::cerr << "single object notify failed\n";
        return 1;
    }

    const char *many = "{\"action\":\"update\",\"type\":\"customer\",\"keys\":{},"
                       "\"row\":[{\"id\":1,\"name\":\"Ann\"},{\"id\":2,\"name\":\"Bob\"}]}";
    if (parseDbNotify(many, rows) != 2) {
        std::cerr << "array notify count " << rows.size() << "\n";
        return 2;
    }
    if (rows[0].row_json.find("Ann") == std::string::npos ||
        rows[1].row_json.find("Bob") == std::string::npos) {
        std::cerr << "array notify rows wrong\n";
        return 3;
    }

    const char *del = "{\"action\":\"delete\",\"type\":\"customer\",\"keys\":{\"id\":2}}";
    if (parseDbNotify(del, rows) != 1 || rows[0].action != "delete" ||
        rows[0].type != "customer" || rows[0].keys_json.find("2") == std::string::npos) {
        std::cerr << "delete notify parse failed\n";
        return 4;
    }

    if (parseDbNotify("not json", rows) != 0) {
        std::cerr << "bad json should yield 0\n";
        return 5;
    }
    std::cout << "ok\n";
    return 0;
}
