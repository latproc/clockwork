#include "DbNotify.h"
#include "cJSON.h"
#include <cstring>

int parseDbNotify(const std::string &payload, std::vector<DbNotifyRow> &out) {
    out.clear();
    cJSON *note = cJSON_Parse(payload.c_str());
    if (!note) {
        return 0;
    }
    cJSON *type_json = cJSON_GetObjectItem(note, "type");
    const char *type = (type_json && type_json->type == cJSON_String) ? type_json->valuestring : 0;
    cJSON *action_json = cJSON_GetObjectItem(note, "action");
    const char *action =
        (action_json && action_json->type == cJSON_String) ? action_json->valuestring : "";
    cJSON *keys = cJSON_GetObjectItem(note, "keys");
    cJSON *row = cJSON_GetObjectItem(note, "row");
    const bool is_delete = action && strcmp(action, "delete") == 0;
    if (!type || (!row && !is_delete)) {
        cJSON_Delete(note);
        return 0;
    }
    char *keys_s = keys ? cJSON_PrintUnformatted(keys) : strdup("{}");
    auto push_row = [&](cJSON *one) {
        DbNotifyRow item;
        item.action = action ? action : "";
        item.type = type;
        item.keys_json = keys_s ? keys_s : "{}";
        if (one) {
            char *row_s = cJSON_PrintUnformatted(one);
            if (row_s) {
                item.row_json = row_s;
                free(row_s);
            }
        }
        out.push_back(item);
    };
    if (!row) {
        push_row(0);
    }
    else if (row->type == cJSON_Array) {
        cJSON *child = row->child;
        while (child) {
            push_row(child);
            child = child->next;
        }
    }
    else {
        push_row(row);
    }
    free(keys_s);
    cJSON_Delete(note);
    return static_cast<int>(out.size());
}
