#pragma once

#include <string>
#include <vector>

// dbsvr PUB payload: {action,type,keys,row} where row is one object or an array.
struct DbNotifyRow {
    std::string action;
    std::string type;
    std::string keys_json;
    std::string row_json;
};

int parseDbNotify(const std::string &payload, std::vector<DbNotifyRow> &out);
