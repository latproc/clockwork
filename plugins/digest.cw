DIGEST MACHINE {
    OPTION Algorithm "SHA512";
    OPTION Input "";
    OPTION Result "";
    OPTION Errors "";

    PLUGIN "digest.so.1.0";

    Start STATE;
    Error STATE;
    Done STATE;
    Idle INITIAL;

    COMMAND calculate WITHIN Idle, Done, Error {
        Result := "";
        Errors := "";
        SET SELF TO Start;
    }

    COMMAND reset {
        Input := "";
        Result := "";
        Errors := "";
        SET SELF TO Idle;
    }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <debug_malloc.c>
#include <exec_digest.c>

PLUGIN_EXPORT int check_states(void *scope) {
    return exec_digest(scope);
}

PLUGIN_EXPORT int poll_actions(void *scope) {
    return PLUGIN_COMPLETED;
}
%END_PLUGIN
