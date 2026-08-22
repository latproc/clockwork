WEBREQUEST MACHINE {
    OPTION Request "";     # URL
    OPTION Status 0;       # HTTP status or errno
    OPTION Result "";      # Response body
    OPTION Errors "";      # Errors / diagnostics
    OPTION PostData "";    # POST data
    OPTION ContentType NULL;
    OPTION Method NULL;
    OPTION Headers JSON_VALUE {}; # Additional HTTP headers (string values only)
    OPTION ResponseHeaders JSON_VALUE {}; # Final response headers

    OPTION TrustHostCert FALSE; # Whether to trust host certificate

    PLUGIN "web_request.so.1.0";

    Running STATE;
    Start STATE;
    Error STATE;
    Done STATE;
    Idle INITIAL;

    COMMAND start WITHIN Error { Status := 0; Result := ""; Errors := ""; ResponseHeaders := JSON_VALUE {}; SET SELF TO Start; }
    COMMAND start WITHIN Done  { Status := 0; Result := ""; Errors := ""; ResponseHeaders := JSON_VALUE {}; SET SELF TO Start; }
    COMMAND start WITHIN Idle  { Status := 0; Result := ""; Errors := ""; ResponseHeaders := JSON_VALUE {}; SET SELF TO Start; }
    COMMAND reset { Status := 0; Request := ""; Result := ""; Errors := ""; PostData := ""; ResponseHeaders := JSON_VALUE {}; SET SELF TO Idle; }
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
