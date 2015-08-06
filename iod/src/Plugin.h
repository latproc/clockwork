#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#define PLUGIN_EXPORT __attribute__((visibility("default")))

#define PLUGIN_COMPLETED 0
#define NO_PLUGIN_AVAILABLE 1 /* no plugin matches the command */
#define PLUGIN_ERROR 2 /* the plugin exists but the plugin function could not be found */

#ifdef __cplusplus
extern "C" {
#endif

typedef void * cwpi_Scope;
    
typedef int (*plugin_func)(cwpi_Scope);
typedef long (*plugin_filter)(cwpi_Scope, long);

void *getNamedScope(cwpi_Scope, const char *name);
int getIntValue(cwpi_Scope, const char *property_name, const long **val);
char *getStringValue(cwpi_Scope, const char *property_name);
void setIntValue(cwpi_Scope, const char *property_name, long new_value);
void setStringValue(cwpi_Scope, const char *property_name, const char *new_value);
int changeState(cwpi_Scope, const char *new_state);
char *getState(cwpi_Scope);
void log_message(cwpi_Scope, const char *);

void *getInstanceData(cwpi_Scope);
void setInstanceData(cwpi_Scope, void *block);
    
#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
#include "value.h"

/*
class PluginScope {
public:
    virtual Value &getValue(std::string property);
    virtual void setValue(const std::string &property, Value new_value);
    int changeState(const std::string new_state);
    virtual std::string getState();
    void *getInstanceData() { return data; }
    void setInstanceData(void *block) { data = block; }
    void message(const char *msg);

    PluginScope(MachineInstance *m) : mi(m), data(0) { }
private:
    MachineInstance *mi;
    void * data;
};

struct PluginResult {
    int result_code;
    std::string message;
    PluginResult() : result_code(0), message("") {  }
    PluginResult(int rc) : result_code(rc), message("") {  }
    PluginResult(int rc, const char *msg) : result_code(0), message(msg) {  }
};
*/

class MachineInstance;
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

#endif
