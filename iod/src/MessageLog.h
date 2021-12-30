#ifndef __MessageLog_h__
#define __MessageLog_h__

#include "boost/thread/mutex.hpp"
#include <fstream>
#include <list>
#include <sstream>
#include <string>

#include "cJSON.h"

class LogEntry {
  public:
    LogEntry(const char *msg) : message(msg) { size_ = strlen(msg) + 1 + sizeof(LogEntry); }
    const std::string &entry() const { return message; }
    size_t size() { return size_; }
    cJSON *toJSON() const {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "message", message.c_str());
        return res;
    }
    const std::string &getText() const { return message; }

  private:
    size_t size_;
    std::string message;
};

class MessageLog {
  public:
    static MessageLog *instance();
    static void setMaxMemory(unsigned int max) { max_memory = max; }
    void add(const char *text);
    std::string add(const std::string a = "", const std::string b = "", const std::string c = "",
                    const std::string d = "");
    void purge();

    size_t count() const;

    cJSON *toJSON(unsigned int num) const;
    char *toString(unsigned int num) const;

    void load(const char *filename);

    void save(const char *filename) const;

  private:
    MessageLog();
    MessageLog(const MessageLog &);
    MessageLog operator=(const MessageLog &) const;
    static MessageLog *instance_;
    static unsigned int max_memory;
    unsigned int current_memory;
    std::list<LogEntry *> entries;
    static boost::mutex mutex_;
};

#endif
