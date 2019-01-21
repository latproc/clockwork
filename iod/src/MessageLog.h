#ifndef __MessageLog_h__
#define __MessageLog_h__

#include <string>
#include <list>
#include <fstream>
#include <sstream>
#include "boost/thread/mutex.hpp"

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
		std::string add(const std::string a="", const std::string b="", const std::string c="", const std::string d="");
    void purge();
    
    size_t count() const;
    
    cJSON *toJSON(unsigned int num) const;
    char *toString(unsigned int num) const;
    
    void load(const char *filename);
    
    void save(const char *filename) const;

    std::ostream &get_stream(); // locks access to the message stream and returns it
    void release_stream(); // adds the message stream buffer to the log and releases the lock
    std::string access_stream_message() const; // access the content of the message stream buffer
    void close_stream(); // releases the lock but does not add the message stream buffer to the log
    
private:
    MessageLog();
    MessageLog(const MessageLog&);
    MessageLog operator=(const MessageLog&) const;
    static MessageLog *instance_;
    static unsigned int max_memory;
    unsigned int current_memory;
    std::list<LogEntry*> entries;
    static boost::mutex mutex_;
    std::stringstream output;
};


#endif

