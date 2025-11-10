WebRequest MACHINE {
    OPTION Request "";     # URL
    OPTION Status 0;       # HTTP status or errno
    OPTION Result "";      # Response body
    OPTION Errors "";      # Errors / diagnostics
    OPTION PostData "";    # POST data

    OPTION TrustHostCert FALSE; # Whether to trust host certificate

    PLUGIN "web_request.so.1.0";

    Running STATE;
    Start STATE;
    Error STATE;
    Done STATE;
    Idle INITIAL;

    COMMAND start WITHIN Error { SET SELF TO Start; }
    COMMAND start WITHIN Done  { SET SELF TO Start; }
    COMMAND start WITHIN Idle  { SET SELF TO Start; }
    COMMAND stop WITHIN Running { SET SELF TO Idle; }
    COMMAND reset { Request := ""; Result := ""; SET SELF TO Idle; }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <debug_malloc.c>
#include <exec_web_request.c>
#include <read_file.c>

PLUGIN_EXPORT int check_states(void *scope) {
    return exec_web_request(scope);
}

PLUGIN_EXPORT int poll_actions(void *scope) {
    return PLUGIN_COMPLETED;
}
%END_PLUGIN
