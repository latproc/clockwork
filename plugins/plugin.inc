#include "Plugin.h"
#include "value.h"

extern "C" {
	int check_states(void *scope);
	int poll_actions(void *scope);
}

int CheckStates(PluginScope*);
int PollActions(PluginScope*);

extern "C" {
PLUGIN_EXPORT
int check_states(void *scope) { return CheckStates((PluginScope*)scope); }
PLUGIN_EXPORT
int poll_actions(void *scope) { return PollActions((PluginScope*)scope); }
}

