#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct cJSON;

namespace dbwatch {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

enum class RequestKind {
    Passthrough,
    WatchSet,
    WatchQueryAndRemove,
    WatchStatus,
    WatchList,
    WatchRemove
};

struct RequestPlan {
    RequestKind kind = RequestKind::Passthrough;
    std::string watch_id;
    std::string respond_to;
    std::string machine;
    std::string property;
    std::string database_json;
    std::string table;
    std::vector<std::string> tables;
    int coalesce_ms = 50;
    int renew_ms = 5000;
    int lease_ms = 15000;
};

struct Status {
    std::string watch_id;
    std::string respond_to;
    std::vector<std::string> tables;
    std::string status;
    std::string last_error;
    uint64_t generation = 0;
    uint64_t refresh_count = 0;
    uint64_t error_count = 0;
    int64_t registered_age_ms = 0;
    int64_t last_refresh_age_ms = -1;
    int64_t last_notification_age_ms = -1;
    bool status_transition = false;
};

struct RefreshRequest {
    std::string watch_id;
    std::string respond_to;
    std::string database_json;
    uint64_t notification_revision = 0;
};

class Registry {
  public:
    bool prepare(cJSON *request, TimePoint now, RequestPlan &plan,
                 std::string &error) const;

    Status activated(const RequestPlan &plan, TimePoint now);
    Status activation_failed(const RequestPlan &plan, TimePoint now,
                             const std::string &error);
    Status removed(const std::string &watch_id, TimePoint now,
                   std::string &error);
    Status query_and_remove(const RequestPlan &plan, TimePoint now,
                            std::string &error);

    std::vector<Status> note_table_changed(const std::string &table,
                                           TimePoint now);
    std::vector<RefreshRequest> due_refreshes(TimePoint now);
    std::vector<Status> refresh_succeeded(const RefreshRequest &request,
                                          TimePoint now);
    std::vector<Status> refresh_failed(const RefreshRequest &request,
                                       TimePoint now,
                                       const std::string &error);
    std::vector<Status> expire(TimePoint now);

    bool status(const std::string &watch_id, TimePoint now, Status &result,
                std::string &error) const;
    std::vector<Status> list(TimePoint now) const;
    void mark_all_dirty(TimePoint now);
    void clear();
    size_t size() const;

  private:
    struct Watch {
        RequestPlan plan;
        TimePoint registered_at;
        TimePoint last_renewed;
        TimePoint last_refresh;
        TimePoint last_notification;
        TimePoint dirty_since;
        TimePoint stale_at;
        TimePoint next_attempt;
        std::string status = "active";
        std::string last_error;
        uint64_t generation = 1;
        uint64_t refresh_count = 0;
        uint64_t error_count = 0;
        uint64_t notification_revision = 0;
        uint64_t served_revision = 0;
        unsigned retry_count = 0;
        bool dirty = false;
        bool inflight = false;
        bool has_refreshed = false;
        bool has_notification = false;
    };

    Status snapshot(const Watch &watch, TimePoint now,
                    bool status_transition = false) const;
    std::map<std::string, Watch>::iterator find_watch(const std::string &id);
    std::map<std::string, Watch>::const_iterator find_watch(
        const std::string &id) const;

    std::map<std::string, Watch> watches_;
};

std::string statuses_json(const std::vector<Status> &statuses);
std::string status_json(const Status &status);

} // namespace dbwatch
