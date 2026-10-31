#include "DbWatch.h"

#include "cJSON.h"

#include <algorithm>
#include <cstdlib>

namespace dbwatch {
namespace {

int int_option(cJSON *object, const char *name, int fallback) {
    cJSON *item = object ? cJSON_GetObjectItem(object, name) : nullptr;
    if (!item) {
        return fallback;
    }
    if (item->type == cJSON_Number) {
        if (item->valueNumber.kind == cJSON_Number_int_t) {
            return static_cast<int>(item->valueNumber.val._int);
        }
        return static_cast<int>(item->valueNumber.val._double);
    }
    if (item->type == cJSON_String && item->valuestring) {
        char *end = nullptr;
        long value = std::strtol(item->valuestring, &end, 10);
        if (end != item->valuestring && *end == '\0') {
            return static_cast<int>(value);
        }
    }
    return fallback;
}

std::string string_option(cJSON *object, const char *name) {
    cJSON *item = object ? cJSON_GetObjectItem(object, name) : nullptr;
    if (item && item->type == cJSON_String && item->valuestring) {
        return item->valuestring;
    }
    return "";
}

bool split_target(const std::string &respond_to, std::string &machine,
                  std::string &property) {
    if (respond_to.empty()) {
        return false;
    }
    size_t dot = respond_to.rfind('.');
    if (dot == std::string::npos) {
        machine = respond_to;
        property = "response";
        return true;
    }
    if (dot == 0 || dot + 1 >= respond_to.size()) {
        return false;
    }
    machine = respond_to.substr(0, dot);
    property = respond_to.substr(dot + 1);
    return true;
}

bool append_tables(cJSON *watch, const std::string &query_type,
                   std::vector<std::string> &tables, std::string &error) {
    cJSON *value = watch ? cJSON_GetObjectItem(watch, "tables") : nullptr;
    if (!value && !query_type.empty()) {
        tables.push_back(query_type);
        return true;
    }
    if (value && value->type == cJSON_String && value->valuestring &&
        value->valuestring[0]) {
        tables.push_back(value->valuestring);
        return true;
    }
    if (value && value->type == cJSON_Array) {
        for (cJSON *item = value->child; item; item = item->next) {
            if (item->type != cJSON_String || !item->valuestring ||
                !item->valuestring[0]) {
                error = "watch.tables must contain non-empty strings";
                return false;
            }
            tables.push_back(item->valuestring);
        }
        if (!tables.empty()) {
            return true;
        }
    }
    error = "watch.tables must name at least one source table";
    return false;
}

int clamp_int(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

int64_t age_ms(TimePoint now, TimePoint then, bool present = true) {
    if (!present) {
        return -1;
    }
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - then);
    return age.count();
}

std::string make_database_json(cJSON *request) {
    char *source = cJSON_PrintUnformatted(request);
    cJSON *copy = source ? cJSON_Parse(source) : nullptr;
    free(source);
    if (!copy) {
        return "";
    }
    cJSON_DeleteItemFromObject(copy, "watch");
    char *encoded = cJSON_PrintUnformatted(copy);
    std::string result = encoded ? encoded : "";
    free(encoded);
    cJSON_Delete(copy);
    return result;
}

} // namespace

bool Registry::prepare(cJSON *request, TimePoint now, RequestPlan &plan,
                       std::string &error) const {
    (void)now;
    plan = RequestPlan{};
    error.clear();
    if (!request) {
        return true;
    }
    if (request->type == cJSON_Array) {
        for (cJSON *item = request->child; item; item = item->next) {
            if (item->type == cJSON_Object &&
                (cJSON_GetObjectItem(item, "watch") ||
                 string_option(item, "action").compare(0, 6, "watch_") == 0)) {
                error = "watch queries cannot be inside a batch";
                return false;
            }
        }
        return true;
    }
    if (request->type != cJSON_Object) {
        return true;
    }

    std::string action = string_option(request, "action");
    if (action == "watch_status" || action == "watch_list" ||
        action == "watch_remove") {
        plan.respond_to = string_option(request, "respond_to");
        plan.watch_id = string_option(request, "watch_id");
        if (plan.watch_id.empty()) {
            plan.watch_id = string_option(request, "id");
        }
        if (action == "watch_status" || action == "watch_remove") {
            if (plan.watch_id.empty()) {
                error = "watch id is required";
                return false;
            }
            plan.kind = action == "watch_status" ? RequestKind::WatchStatus
                                                 : RequestKind::WatchRemove;
        }
        else {
            plan.kind = RequestKind::WatchList;
        }
        return true;
    }

    if (action != "select") {
        return true;
    }

    cJSON *watch = cJSON_GetObjectItem(request, "watch");
    if (!watch) {
        return true;
    }

    plan.respond_to = string_option(request, "respond_to");
    if (!split_target(plan.respond_to, plan.machine, plan.property)) {
        error = "watch query requires respond_to machine[.property]";
        return false;
    }

    if (watch->type == cJSON_False) {
        plan.kind = RequestKind::WatchQueryAndRemove;
        plan.watch_id = plan.respond_to;
        plan.database_json = make_database_json(request);
        if (plan.database_json.empty()) {
            error = "could not encode watch removal query";
            return false;
        }
        return true;
    }
    if (watch->type != cJSON_True && watch->type != cJSON_Object) {
        error = "watch must be true, false or an object";
        return false;
    }

    cJSON *watch_object = watch->type == cJSON_Object ? watch : nullptr;
    plan.watch_id = string_option(watch_object, "id");
    if (plan.watch_id.empty()) {
        plan.watch_id = plan.respond_to;
    }
    plan.table = string_option(request, "type");
    if (plan.table.empty()) {
        error = "watch query requires a table or view type";
        return false;
    }
    if (!append_tables(watch_object, plan.table, plan.tables, error)) {
        return false;
    }
    plan.coalesce_ms =
        clamp_int(int_option(watch_object, "coalesce_ms", 50), 0, 60000);
    plan.renew_ms =
        clamp_int(int_option(watch_object, "renew_ms", 5000), 100, 300000);
    plan.lease_ms =
        clamp_int(int_option(watch_object, "lease_ms", 15000), 200, 900000);
    if (plan.lease_ms <= plan.renew_ms) {
        error = "watch.lease_ms must be greater than watch.renew_ms";
        return false;
    }
    plan.database_json = make_database_json(request);
    if (plan.database_json.empty()) {
        error = "could not encode watch query";
        return false;
    }
    plan.kind = RequestKind::WatchSet;
    return true;
}

Status Registry::activated(const RequestPlan &plan, TimePoint now) {
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (it->first != plan.respond_to &&
            it->second.plan.watch_id == plan.watch_id) {
            it = watches_.erase(it);
        }
        else {
            ++it;
        }
    }
    auto found = watches_.find(plan.respond_to);
    bool transition = false;
    if (found == watches_.end()) {
        Watch watch;
        watch.plan = plan;
        watch.registered_at = now;
        watch.last_renewed = now;
        watch.last_refresh = now;
        watch.status = "active";
        watch.generation = 1;
        watch.has_refreshed = true;
        transition = true;
        found = watches_.insert(std::make_pair(plan.respond_to, watch)).first;
    }
    else {
        Watch &watch = found->second;
        transition = watch.status != "active";
        watch.plan = plan;
        watch.status = "active";
        watch.last_error.clear();
        watch.last_renewed = now;
        watch.last_refresh = now;
        watch.has_refreshed = true;
        watch.retry_count = 0;
        watch.inflight = false;
        watch.dirty = false;
        watch.served_revision = watch.notification_revision;
        ++watch.generation;
    }
    return snapshot(found->second, now, transition);
}

Status Registry::activation_failed(const RequestPlan &plan, TimePoint now,
                                   const std::string &error) {
    Status result;
    result.watch_id = plan.watch_id;
    result.respond_to = plan.respond_to;
    result.tables = plan.tables;
    result.status = "error";
    result.last_error = error;
    result.registered_age_ms = 0;
    result.status_transition = true;
    (void)now;
    return result;
}

Status Registry::removed(const std::string &watch_id, TimePoint now,
                         std::string &error) {
    auto found = find_watch(watch_id);
    if (found == watches_.end()) {
        error = "watch not found";
        return Status{};
    }
    Status result = snapshot(found->second, now, true);
    result.status = "removed";
    watches_.erase(found);
    return result;
}

Status Registry::query_and_remove(const RequestPlan &plan, TimePoint now,
                                  std::string &error) {
    auto found = watches_.find(plan.respond_to);
    if (found == watches_.end()) {
        if (!plan.watch_id.empty()) {
            found = find_watch(plan.watch_id);
        }
    }
    if (found == watches_.end()) {
        Status result;
        result.watch_id = plan.watch_id;
        result.respond_to = plan.respond_to;
        result.status = "removed";
        result.status_transition = false;
        return result;
    }
    Status result = snapshot(found->second, now, true);
    result.status = "removed";
    watches_.erase(found);
    error.clear();
    return result;
}

std::vector<Status>
Registry::note_table_changed(const std::string &table, TimePoint now) {
    std::vector<Status> results;
    for (auto &entry : watches_) {
        Watch &watch = entry.second;
        if (std::find(watch.plan.tables.begin(), watch.plan.tables.end(),
                      table) == watch.plan.tables.end()) {
            continue;
        }
        watch.has_notification = true;
        watch.last_notification = now;
        ++watch.notification_revision;
        if (!watch.dirty) {
            watch.dirty = true;
            watch.dirty_since = now;
        }
    }
    return results;
}

std::vector<RefreshRequest> Registry::due_refreshes(TimePoint now) {
    std::vector<RefreshRequest> due;
    for (auto &entry : watches_) {
        Watch &watch = entry.second;
        if ((watch.status != "active" && watch.status != "error") ||
            watch.inflight || !watch.dirty) {
            continue;
        }
        bool coalesced = now - watch.dirty_since >=
                         std::chrono::milliseconds(watch.plan.coalesce_ms);
        bool retry_due =
            watch.status == "error" ? now >= watch.next_attempt : true;
        if (!coalesced || !retry_due) {
            continue;
        }
        watch.inflight = true;
        RefreshRequest request;
        request.watch_id = watch.plan.watch_id;
        request.respond_to = watch.plan.respond_to;
        request.database_json = watch.plan.database_json;
        request.notification_revision = watch.notification_revision;
        due.push_back(request);
    }
    return due;
}

std::vector<Status>
Registry::refresh_succeeded(const RefreshRequest &request, TimePoint now) {
    std::vector<Status> results;
    auto found = watches_.find(request.respond_to);
    if (found == watches_.end()) {
        return results;
    }
    Watch &watch = found->second;
    watch.inflight = false;
    watch.last_refresh = now;
    watch.has_refreshed = true;
    watch.served_revision = request.notification_revision;
    if (watch.notification_revision == request.notification_revision) {
        watch.dirty = false;
    }
    watch.retry_count = 0;
    ++watch.refresh_count;
    bool transition = watch.status != "active";
    watch.status = "active";
    watch.last_error.clear();
    if (transition) {
        results.push_back(snapshot(watch, now, true));
    }
    return results;
}

std::vector<Status>
Registry::refresh_failed(const RefreshRequest &request, TimePoint now,
                         const std::string &error) {
    std::vector<Status> results;
    auto found = watches_.find(request.respond_to);
    if (found == watches_.end()) {
        return results;
    }
    Watch &watch = found->second;
    watch.inflight = false;
    watch.status = "error";
    watch.last_error = error;
    ++watch.error_count;
    ++watch.retry_count;
    int delay_ms = 250;
    for (unsigned i = 1; i < watch.retry_count && i < 6; ++i) {
        delay_ms *= 2;
    }
    delay_ms = std::min(delay_ms, 5000);
    watch.next_attempt = now + std::chrono::milliseconds(delay_ms);
    results.push_back(snapshot(watch, now, true));
    return results;
}

std::vector<Status> Registry::expire(TimePoint now) {
    std::vector<Status> results;
    for (auto it = watches_.begin(); it != watches_.end();) {
        Watch &watch = it->second;
        auto leased = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - watch.last_renewed);
        if (leased.count() < watch.plan.lease_ms) {
            ++it;
            continue;
        }
        if (watch.status != "stale") {
            watch.status = "stale";
            watch.stale_at = now;
            watch.last_error = "watch lease expired";
            results.push_back(snapshot(watch, now, true));
            ++it;
            continue;
        }
        auto stale_for = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - watch.stale_at);
        if (stale_for.count() < watch.plan.lease_ms) {
            ++it;
            continue;
        }
        Status removed_watch = snapshot(watch, now, true);
        removed_watch.status = "removed";
        removed_watch.last_error = "watch lease expired";
        results.push_back(removed_watch);
        it = watches_.erase(it);
    }
    return results;
}

bool Registry::status(const std::string &watch_id, TimePoint now,
                      Status &result, std::string &error) const {
    auto found = find_watch(watch_id);
    if (found == watches_.end()) {
        error = "watch not found";
        return false;
    }
    result = snapshot(found->second, now);
    return true;
}

std::vector<Status> Registry::list(TimePoint now) const {
    std::vector<Status> result;
    for (const auto &entry : watches_) {
        result.push_back(snapshot(entry.second, now));
    }
    return result;
}

void Registry::mark_all_dirty(TimePoint now) {
    for (auto &entry : watches_) {
        Watch &watch = entry.second;
        watch.last_notification = now;
        ++watch.notification_revision;
        if (!watch.dirty) {
            watch.dirty_since = now;
        }
        watch.dirty = true;
    }
}

void Registry::clear() { watches_.clear(); }

size_t Registry::size() const { return watches_.size(); }

Status Registry::snapshot(const Watch &watch, TimePoint now,
                          bool status_transition) const {
    Status result;
    result.watch_id = watch.plan.watch_id;
    result.respond_to = watch.plan.respond_to;
    result.tables = watch.plan.tables;
    result.status = watch.status;
    result.last_error = watch.last_error;
    result.generation = watch.generation;
    result.refresh_count = watch.refresh_count;
    result.error_count = watch.error_count;
    result.registered_age_ms = age_ms(now, watch.registered_at);
    result.last_refresh_age_ms =
        age_ms(now, watch.last_refresh, watch.has_refreshed);
    result.last_notification_age_ms =
        age_ms(now, watch.last_notification, watch.has_notification);
    result.status_transition = status_transition;
    return result;
}

std::map<std::string, Registry::Watch>::iterator
Registry::find_watch(const std::string &id) {
    auto by_target = watches_.find(id);
    if (by_target != watches_.end()) {
        return by_target;
    }
    for (auto it = watches_.begin(); it != watches_.end(); ++it) {
        if (it->second.plan.watch_id == id) {
            return it;
        }
    }
    return watches_.end();
}

std::map<std::string, Registry::Watch>::const_iterator
Registry::find_watch(const std::string &id) const {
    auto by_target = watches_.find(id);
    if (by_target != watches_.end()) {
        return by_target;
    }
    for (auto it = watches_.begin(); it != watches_.end(); ++it) {
        if (it->second.plan.watch_id == id) {
            return it;
        }
    }
    return watches_.end();
}

std::string statuses_json(const std::vector<Status> &statuses) {
    cJSON *array = cJSON_CreateArray();
    for (const auto &status : statuses) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "watch_id", status.watch_id.c_str());
        cJSON_AddStringToObject(item, "respond_to", status.respond_to.c_str());
        cJSON *tables = cJSON_CreateArray();
        for (const auto &table : status.tables) {
            cJSON_AddItemToArray(tables, cJSON_CreateString(table.c_str()));
        }
        cJSON_AddItemToObject(item, "tables", tables);
        cJSON_AddStringToObject(item, "status", status.status.c_str());
        cJSON_AddStringToObject(item, "watch_error", status.last_error.c_str());
        cJSON_AddNumberToObject(item, "watch_generation",
                                static_cast<double>(status.generation));
        cJSON_AddNumberToObject(item, "refresh_count",
                                static_cast<double>(status.refresh_count));
        cJSON_AddNumberToObject(item, "error_count",
                                static_cast<double>(status.error_count));
        cJSON_AddNumberToObject(item, "registered_age_ms",
                                static_cast<double>(status.registered_age_ms));
        cJSON_AddNumberToObject(item, "last_refresh_age_ms",
                                static_cast<double>(status.last_refresh_age_ms));
        cJSON_AddNumberToObject(item, "last_notification_age_ms",
                                static_cast<double>(status.last_notification_age_ms));
        cJSON_AddItemToArray(array, item);
    }
    char *encoded = cJSON_PrintUnformatted(array);
    std::string result = encoded ? encoded : "[]";
    free(encoded);
    cJSON_Delete(array);
    return result;
}

std::string status_json(const Status &status) {
    return statuses_json(std::vector<Status>{status});
}

} // namespace dbwatch
