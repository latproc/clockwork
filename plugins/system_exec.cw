# SYSTEMEXEC — run a shell-style command string via the iod plugin path.
#
# CW surface matches GenericLib (plant): options, states, and commands used by
# generic_bps / generic_panel / generic_channel_monitor / DriveTemp, etc.
#
# Implementation: thin wrapper around iod/src/exec_command.c (and helpers).
# Do not re-embed the full C body here — rebuild the .so after iod changes.
#
# While the command is running the machine stays in Running until Done or Error;
# CommandStatus is 0 on success, otherwise the process exit status / errno.

SYSTEMEXEC MACHINE {
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

%BEGIN_PLUGIN
#include <Plugin.h>
#include <copy_environment.c>
#include <debug_malloc.c>
#include <exec_command.c>
#include <read_file.c>
#include <split_string.c>

PLUGIN_EXPORT int check_states(void *scope) {
    return exec_command(scope);
}

PLUGIN_EXPORT int poll_actions(void *scope) {
    return PLUGIN_COMPLETED;
}
%END_PLUGIN
