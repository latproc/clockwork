#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include "value.h"

#define PLUGIN_EXPORT __attribute__((visibility("default")))

#define PLUGIN_COMPLETED 0
#define NO_PLUGIN_AVAILABLE 1 /* no plugin matches the command */
#define PLUGIN_ERROR 2 /* the plugin exists but the plugin function could not be found */

class PluginData {
    virtual ~PluginData() { }
};


class PluginScope {
public:
    virtual Value &getValue(std::string property);
    virtual void setValue(const std::string &property, Value new_value);
    bool changeState(const std::string new_state);
    PluginData *getInstanceData() { return data; }
    void setInstanceData(PluginData *block) { data = block; }

    PluginScope() : data(0) { }
private:
    PluginData * data;
};

struct PluginResult {
    int result_code;
    std::string message;
    PluginResult() : result_code(0), message("") {  }
    PluginResult(int rc) : result_code(rc), message("") {  }
    PluginResult(int rc, const char *msg) : result_code(0), message(msg) {  }
};

extern "C" {
typedef int (*plugin_func)(PluginScope *);
typedef long (*plugin_filter)(PluginScope *, long);
}

class Plugin {
public:
    plugin_func state_check;
    plugin_func poll_actions;
    plugin_filter filter;
    
    Plugin() : state_check(0), poll_actions(0), filter(0) { }
    Plugin(plugin_func sc, plugin_func pa, plugin_filter f);
};

class PluginManager {
public:
    static PluginManager *instance();
    ~PluginManager() { }
    void *findPlugin(const std::string name);
    void registerPlugin(const std::string name, void *handle);
    
private:
    static PluginManager *_instance;
    PluginManager();
    std::map<std::string, void *>plugins;
};


#endif
