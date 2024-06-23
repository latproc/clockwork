#This plugin attempts to execute the command set in its Command property
#when it is in the Start state.
#While the command is running the machine stays in the Running state until
#eventually it moved to Error or Done at which time CommandStatus reflects
#the return result from the command(0 for no error)

SystemExec MACHINE {
    OPTION Command "";
    OPTION CommandStatus 0;
    OPTION Result "";
    OPTION Errors "";
    PLUGIN "system_exec.so.1.0";

    Running STATE;
    Start STATE;
    Error STATE;
    Done STATE;
    Idle INITIAL;

    COMMAND start WITHIN Error { SET SELF TO Start; }
    COMMAND start WITHIN Done { SET SELF TO Start; }
    COMMAND start WITHIN Idle { SET SELF TO Start; }
    COMMAND reset WITHIN Error { SET SELF TO Idle; }
}

% BEGIN_PLUGIN
#include <Plugin.h>
#include <copy_environment.c>
#include <debug_malloc.c>
#include <exec_command.c>
#include <read_file.c>
#include <split_string.c>

        PLUGIN_EXPORT int
        check_states(void *scope) {
    return exec_command(scope);
}

PLUGIN_EXPORT
int poll_actions(void *scope) { return PLUGIN_COMPLETED; }

% END_PLUGIN
