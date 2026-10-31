#include "DbWatch.h"

#include "cJSON.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

cJSON *parse(const char *json) {
    cJSON *result = cJSON_Parse(json);
    if (!result) {
        std::cerr << "could not parse test JSON\n";
        std::exit(90);
    }
    return result;
}

} // namespace

int main() {
    using namespace dbwatch;
    Registry registry;
    auto now = Clock::now();
    RequestPlan plan;
    std::string error;

    cJSON *request = parse(
        "{\"action\":\"select\",\"type\":\"bale_instances\","
        "\"where\":{\"station\":\"GrabChamber\"},\"respond_to\":\"M_GrabView.response\","
        "\"watch\":{\"id\":\"grab_queue\",\"tables\":[\"bale_instances\"],"
        "\"coalesce_ms\":50,\"renew_ms\":5000,\"lease_ms\":15000}}");
    if (!check(registry.prepare(request, now, plan, error),
               "watch request was rejected")) {
        return 1;
    }
    if (!check(plan.kind == RequestKind::WatchSet, "request did not become a watch") ||
        !check(plan.watch_id == "grab_queue", "watch id was not parsed") ||
        !check(plan.machine == "M_GrabView", "watch machine was not parsed") ||
        !check(plan.coalesce_ms == 50, "watch coalesce was not parsed") ||
        !check(plan.renew_ms == 5000, "watch renew was not parsed") ||
        !check(plan.lease_ms == 15000, "watch lease was not parsed") ||
        !check(plan.tables.size() == 1 &&
                   plan.tables[0] == "bale_instances",
               "watch tables were not parsed") ||
        !check(plan.database_json.find("\"watch\"") == std::string::npos,
               "watch controls were not stripped from the database query")) {
        return 2;
    }

    Status active = registry.activated(plan, now);
    if (!check(active.status == "active", "watch did not become active") ||
        !check(active.generation == 1, "initial generation is not 1") ||
        !check(registry.size() == 1, "active watch was not stored")) {
        return 3;
    }

    auto statuses = registry.list(now);
    if (!check(statuses.size() == 1 && statuses[0].watch_id == "grab_queue",
               "watch list is wrong")) {
        return 4;
    }

    auto unrelated_time = now + std::chrono::milliseconds(10);
    registry.note_table_changed("other_table", unrelated_time);
    if (!check(registry.due_refreshes(
                   unrelated_time + std::chrono::milliseconds(100))
                   .empty(),
               "unrelated table dirtied the watch")) {
        return 5;
    }

    auto changed_time = now + std::chrono::milliseconds(200);
    registry.note_table_changed("bale_instances", changed_time);
    if (!check(registry.due_refreshes(
                   changed_time + std::chrono::milliseconds(20))
                   .empty(),
               "coalescing window was ignored")) {
        return 6;
    }
    registry.note_table_changed(
        "bale_instances", changed_time + std::chrono::milliseconds(30));
    auto due =
        registry.due_refreshes(changed_time + std::chrono::milliseconds(50));
    if (!check(due.size() == 1 && due[0].watch_id == "grab_queue",
               "dirty watch was not due after coalesce") ||
        !check(due[0].database_json.find("\"watch\"") == std::string::npos,
               "refresh retained watch controls")) {
        return 7;
    }
    auto changed = registry.refresh_failed(
        due[0], changed_time + std::chrono::milliseconds(60),
        "temporary failure");
    if (!check(changed.size() == 1 && changed[0].status == "error" &&
                   changed[0].status_transition,
               "refresh failure did not become observable")) {
        return 8;
    }
    if (!check(registry.due_refreshes(
                   changed_time + std::chrono::milliseconds(100))
                   .empty(),
               "failed refresh retried before backoff")) {
        return 9;
    }
    auto retry =
        registry.due_refreshes(changed_time + std::chrono::seconds(2));
    auto recovered = registry.refresh_succeeded(
        retry.empty() ? RefreshRequest{} : retry[0],
        changed_time + std::chrono::seconds(2));
    if (!check(retry.size() == 1, "failed refresh did not retry") ||
        !check(recovered.size() == 1 && recovered[0].status == "active",
               "error to active transition was not reported")) {
        return 10;
    }

    cJSON *status_request = parse(
        "{\"action\":\"watch_status\",\"watch_id\":\"grab_queue\","
        "\"respond_to\":\"M_Status.response\"}");
    RequestPlan status_plan;
    if (!check(registry.prepare(status_request, now, status_plan, error),
               "watch_status request was rejected") ||
        !check(status_plan.kind == RequestKind::WatchStatus,
               "watch_status was not recognised")) {
        return 11;
    }
    Status retrieved;
    if (!check(registry.status("grab_queue", now, retrieved, error),
               "watch status lookup failed") ||
        !check(retrieved.status == "active", "watch status lookup is wrong")) {
        return 12;
    }
    std::string encoded = status_json(retrieved);
    if (!check(encoded.find("\"watch_id\":\"grab_queue\"") !=
                   std::string::npos,
               "status JSON omitted watch id")) {
        return 13;
    }

    cJSON *remove_request = parse(
        "{\"action\":\"watch_remove\",\"watch_id\":\"grab_queue\","
        "\"respond_to\":\"M_Status.response\"}");
    RequestPlan remove_plan;
    if (!check(registry.prepare(remove_request, now, remove_plan, error),
               "watch_remove request was rejected") ||
        !check(remove_plan.kind == RequestKind::WatchRemove,
               "watch_remove was not recognised")) {
        return 14;
    }
    Status removed =
        registry.removed("grab_queue", now, error);
    if (!check(error.empty() && removed.status == "removed" &&
                   registry.size() == 0,
               "watch removal failed")) {
        return 15;
    }

    if (!check(registry.prepare(request, now, plan, error),
               "second watch request was rejected")) {
        return 16;
    }
    registry.activated(plan, now);
    auto stale =
        registry.expire(now + std::chrono::milliseconds(plan.lease_ms + 1));
    if (!check(stale.size() == 1 && stale[0].status == "stale",
               "expired watch did not become stale")) {
        return 17;
    }
    auto removed_after_stale = registry.expire(
        now + std::chrono::milliseconds(plan.lease_ms * 2 + 2));
    if (!check(removed_after_stale.size() == 1 &&
                   removed_after_stale[0].status == "removed" &&
                   registry.size() == 0,
               "stale watch was not removed")) {
        return 18;
    }

    cJSON *no_watch = parse(
        "{\"action\":\"select\",\"type\":\"bale_instances\","
        "\"respond_to\":\"M_GrabView.response\"}");
    RequestPlan no_watch_plan;
    if (!check(registry.prepare(no_watch, now, no_watch_plan, error) &&
                   no_watch_plan.kind == RequestKind::Passthrough,
               "ordinary select was changed by watch handling")) {
        return 19;
    }
    cJSON *remove_watch = parse(
        "{\"action\":\"select\",\"type\":\"bale_instances\","
        "\"respond_to\":\"M_GrabView.response\",\"watch\":false}");
    RequestPlan remove_watch_plan;
    if (!check(registry.prepare(remove_watch, now, remove_watch_plan, error) &&
                   remove_watch_plan.kind == RequestKind::WatchQueryAndRemove,
               "watch:false was not recognised")) {
        return 20;
    }
    cJSON *bad_lease = parse(
        "{\"action\":\"select\",\"type\":\"bale_instances\","
        "\"respond_to\":\"M_GrabView.response\","
        "\"watch\":{\"renew_ms\":5000,\"lease_ms\":1000}}");
    RequestPlan bad_lease_plan;
    if (!check(!registry.prepare(bad_lease, now, bad_lease_plan, error),
               "invalid watch lease was accepted")) {
        return 21;
    }
    cJSON *batch_watch = parse(
        "[{\"action\":\"select\",\"type\":\"bale_instances\","
        "\"respond_to\":\"M_GrabView.response\",\"watch\":true}]");
    RequestPlan batch_plan;
    if (!check(!registry.prepare(batch_watch, now, batch_plan, error),
               "watch inside a batch was accepted")) {
        return 22;
    }

    cJSON_Delete(batch_watch);
    cJSON_Delete(bad_lease);
    cJSON_Delete(remove_watch);
    cJSON_Delete(no_watch);
    cJSON_Delete(remove_request);
    cJSON_Delete(status_request);
    cJSON_Delete(request);
    std::cout << "ok\n";
    return 0;
}
