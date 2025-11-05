#include <Plugin.h>
#include <assert.h>
#include <copy_environment.h>
#include <ctype.h>
#include <debug_malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <read_file.h>
#include <split_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int need_release_params = 0;

struct MyData {
    char **environment;
    char **parameters;
    const int64_t *cmd_status;
    int child;
    char *stdout;
    char *stderr;
};

char *new_temp_filename(const char *prefix, const char *suffix) {
    char tmp_filename[250];
    int res;

    do {
        struct stat fs;
        snprintf(tmp_filename, 250, "%s%04ld%s", prefix, random() % 10000, suffix);
        res = stat(tmp_filename, &fs);
    } while (res != -1);
    return debug_strdup(tmp_filename, "tmp_filename");
}

#define PLUGIN_COMPLETED 0
#define NO_PLUGIN_AVAILABLE 1 /* no plugin matches the command */
#define PLUGIN_ERROR 2        /* the plugin exists but the plugin function could not be found */

int exec_command(void *scope) {
    struct MyData *data = (struct MyData *)getInstanceData(scope);
    char *cmd;
    char *current = 0;
    if (!data) {
        data = (struct MyData *)debug_malloc(sizeof(struct MyData), "data");
        setInstanceData(scope, data);
        data->parameters = 0;
        data->environment = 0;
        data->cmd_status = 0;
        data->child = 0;
        if (!getIntValue(scope, "CommandStatus", &data->cmd_status)) {
            log_message(scope, "SystemExec CommandStatus property is not an integer");
            if (!changeState(scope, "Error")) {
                // assert("exec_command did not immediately transition to error after fork() failed" && 0);
            }
            goto plugin_init_error;
        }

        goto continue_plugin;
    plugin_init_error:
        setInstanceData(scope, 0);
        debug_free(data, "data");
        return PLUGIN_ERROR;
    }

continue_plugin:

    current = getState(scope);
    did_alloc("current");
    if (current && strcmp(current, "Start") == 0 && data->child == 0) {
        cmd = getStringValue(scope, "Command");
        if (cmd && *cmd) {
            did_alloc("cmd");
            if (!changeState(scope, "Running")) {
                //assert("exec_command did not immediately change state to Running" && 0);
                goto CommandFinished;
            }
            data->parameters = split_string(cmd);
            data->environment = copy_environment();
            need_release_params += 2;
            debug_free(cmd, "cmd");

            data->stdout = new_temp_filename("/tmp/sysexec_o_", ".txt");
            data->stderr = new_temp_filename("/tmp/sysexec_e_", ".txt");

            int child;
            if ((child = fork()) == -1) {
                perror("fork");
                setIntValue(scope, "CommandStatus", errno);
                if (!changeState(scope, "Error")) {
                    // assert( "exec_command did not immediately transition to error after fork() failed" && 0);
                }
                data->child = 0; // Does this plugin recover if the above state change fails?
                goto CommandFinished;
            }
            else if (child == 0) { /* child */
                // Redirect stdout and stderr
                int out_fd = open(data->stdout, O_CREAT | O_TRUNC | O_WRONLY, 0640);
                if (out_fd == -1) {
                    perror("create stdout file");
                    exit(3);
                }
                int err_fd = open(data->stderr, O_CREAT | O_TRUNC | O_WRONLY, 0640);
                if (err_fd == -1) {
                    perror("create stderr file");
                    exit(3);
                }
                close(1);
                dup(out_fd);
                close(out_fd);
                close(2);
                dup(err_fd);
                close(err_fd);
                int res = execve(data->parameters[0], data->parameters, data->environment);
                if (res == -1) {
                    perror("execve");
                    release_params(data->parameters);
                    release_params(data->environment);
                    _exit(2);
                }
            }
            else
                data->child = child;
            debug_free(current, "current");
            return PLUGIN_COMPLETED; /* child still running */
        }
        else { /* empty or missing Command property in scope */
            if (!changeState(scope, "Error")) {
                // assert("exec_command did not immediately transition to error due to no command" && 0);
            }
            data->child = 0;
            debug_free(current, "current");
            debug_free(data, "data");
            data = 0;
            return PLUGIN_COMPLETED; /* Nothing left to do) */
        }
    }
    else if (data->child && current && strcmp(current, "Running") == 0) {
        int stat = 0;
        int err = waitpid(data->child, &stat, WNOHANG);
        if (err == -1) {
            perror("waitpid");
            if (errno == ECHILD)
                printf("child exited %d (stat = %d)\n", WEXITSTATUS(stat), stat);
        }
        else if (err == 0) {
            debug_free(current, "current");
            return PLUGIN_COMPLETED; // child still running
        }
        if (stat == 0) {
            setIntValue(scope, "CommandStatus", 0);
            changeState(scope, "Done");
            data->child = 0;
            goto CommandFinished;
        }
        else if (WIFEXITED(stat)) {
            setIntValue(scope, "CommandStatus", WEXITSTATUS(stat));
            changeState(scope, "Error");
            data->child = 0;
            goto CommandFinished;
        }
        else if (WIFSIGNALED(stat)) {
            setIntValue(scope, "CommandStatus", WTERMSIG(stat));
            changeState(scope, "Error");
            data->child = 0;
            goto CommandFinished;
        }
        else if (WIFSTOPPED(stat)) {
            setIntValue(scope, "CommandStatus", WSTOPSIG(stat));
        }
        debug_free(current, "current");
        return PLUGIN_COMPLETED;
    CommandFinished:
        if (data->parameters) {
            release_params(data->parameters);
            --need_release_params;
        }
        if (data->environment) {
            release_params(data->environment);
            --need_release_params;
        }
        data->parameters = 0;
        data->environment = 0;
        if (data->stdout) {
            read_file_to(scope, "Result", data->stdout, setStringValue);
            unlink(data->stdout);
            debug_free(data->stdout, "tmp_filename");
            data->stdout = 0;
        }
        if (data->stderr) {
            read_file_to(scope, "Errors", data->stderr, setStringValue);
            unlink(data->stderr);
            debug_free(data->stderr, "tmp_filename");
            data->stderr = 0;
        }
    }
    setInstanceData(scope, 0);
    debug_free(current, "current");
    debug_free(data, "data");
    return PLUGIN_COMPLETED;
}
