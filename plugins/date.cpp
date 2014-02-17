#include "plugin.inc"
PLUGIN_EXPORT
int CheckStates(PluginScope *scope)
{
    time_t now = time(NULL);
    char *buf = (char*)malloc(30);
    ctime_r(&now, buf);
    scope->setValue("time", buf);
    free(buf);
	scope->changeState("done");
    return PLUGIN_COMPLETED;
}

struct MyData : public PluginData {
	long count;
};

PLUGIN_EXPORT
int PollActions(PluginScope* scope) {
//	MyData *data = scope->getInstanceData();
//	if (!data) { data = new MyData; scope->setInstanceData(data);
	static long count = 0;
	std::string property("count");
    scope->setValue(property, ++count);
	return PLUGIN_COMPLETED;
}


