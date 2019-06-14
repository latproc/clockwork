Test MACHINE {
    PLUGIN "date.so";
    OPTION time "";
	OPTION count 0;
    
    starting INITIAL;
    running STATE;
    done STATE;
ENTER starting { SET SELF TO running }
}
test Test;

%BEGIN_PLUGIN
#include <sys/time.h>
#include <stdlib.h>
#include <Plugin.h>

PLUGIN_EXPORT
int CheckStates(cwpi_Scope scope)
{
    time_t now = time(0);
    char *buf = (char*)malloc(30);
    ctime_r(&now, buf);
    setStringValue(scope, "time", buf);
    free(buf);
	changeState(scope, "done");
    return PLUGIN_COMPLETED;
}

struct MyData {
	long count;
};

PLUGIN_EXPORT
int PollActions(cwpi_Scope scope) {
	struct MyData *data = getInstanceData(scope);
	if (!data) { data = malloc(sizeof(struct MyData)); setInstanceData(scope, data); }
	
	static long count = 0;
	setIntValue(scope, "count", ++count);
	return PLUGIN_COMPLETED;
}

%END_PLUGIN

