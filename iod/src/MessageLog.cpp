#include "MessageLog.h"

#include <sstream>
#include <functional>
#include <algorithm>
#include "Logger.h"

unsigned int MessageLog::max_memory = 1024*1024;
MessageLog * MessageLog::instance_ = 0;
boost::mutex MessageLog::mutex_;

MessageLog::MessageLog(): current_memory(0) {}

MessageLog *MessageLog::instance() {
    if (!instance_) {
        instance_ = new MessageLog();
    }
    return instance_;
}

void MessageLog::add(const char *text) {
	size_t len = 50 + strlen(text);
	char buf[len];
	Logger::getTimeString(buf, 50);

	boost::mutex::scoped_lock lock(mutex_);
	strncat(buf, text, 50);
    size_t extra = strlen(buf) + 1 + sizeof(LogEntry);
    std::list<LogEntry*>::iterator iter = entries.begin();
    while (iter != entries.end()  && current_memory + extra > max_memory) {
        LogEntry *e = *iter;
        current_memory -= e->size();
        iter = entries.erase(iter);
        delete e;
    }
    entries.push_back(new LogEntry(buf));
    current_memory += extra;
}

void MessageLog::purge() {
    boost::mutex::scoped_lock lock(mutex_);
    std::list<LogEntry*>::iterator iter = entries.begin();
    while (iter != entries.end() ) {
        LogEntry *e = *iter;
        iter = entries.erase(iter);
        delete e;
    }
}

size_t MessageLog::count() const {
    return entries.size();
}

cJSON *MessageLog::toJSON(unsigned int num) const {
    boost::mutex::scoped_lock lock(mutex_);
    cJSON *data = cJSON_CreateArray();
    std::list<LogEntry*>::const_iterator iter = entries.begin();
    if (num) {
        long skip = entries.size() - num;
        while (skip-- >0 && iter != entries.end()) iter++;
    }
    while (iter != entries.end()) {
        LogEntry *e = *iter++;
        cJSON_AddItemToArray(data, e->toJSON());
    }
    return data;
}

char *MessageLog::toString(unsigned int num) const {
    boost::mutex::scoped_lock lock(mutex_);
    std::stringstream ss;
    std::list<LogEntry*>::const_iterator iter = entries.begin();
    if (num) {
        long skip = entries.size() - num;
        while (skip-- >0 && iter != entries.end()) iter++;
    }
    while (iter != entries.end() ) {
        LogEntry *e = *iter++;
        ss << e->getText().c_str() << "\n";
    }
    char *res = strdup(ss.str().c_str());
    return res;
}

void MessageLog::save(const char *filename) const {
    boost::mutex::scoped_lock lock(mutex_);
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (file.is_open()) {
        cJSON *data = toJSON(0);
        char *text = cJSON_PrintUnformatted(data);
        cJSON_Delete(data);
        file << text;
        free(text);
        file.close();
    }
}

void MessageLog::load(const char *filename) {
    try {
        boost::mutex::scoped_lock lock(mutex_);
        std::ifstream file(filename);
        if (file.is_open()) {
            std::streampos begin,end;
            begin = file.tellg();
            file.seekg (0, std::ios::end);
            end = file.tellg();
            file.seekg (begin, std::ios::beg);
            char *buf = new char[end-begin + 1];
            file.read(buf, end-begin);
            if (file) {
                buf[file.gcount()] = 0;
                cJSON *data = cJSON_Parse(buf);
                if (data) {
                    if (data->type == cJSON_Array) {
                        int num_entries = cJSON_GetArraySize(data);
                        for (int i = 0; i<num_entries; ++i) {
                            cJSON *item = cJSON_GetArrayItem(data, i);
                            if (item->type == cJSON_String) {
                                add(item->valuestring);
                            }
                            else
                                throw("Unexpected JSON type detected in file");
                        }
                    }
                    cJSON_Delete(data);
                }
            }
            else {
                delete[] buf;
                throw("Error reading from error log file.");
            }
            file.close();
        }
        else {
            throw("Error opening error log file");
        }
    }
    catch(const char *err) {
        add(err);
    }
}
