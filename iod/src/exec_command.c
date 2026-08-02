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

static void release_run_resources(struct MyData *data) {
    if (!data) {
        return;
    }
    if (data->parameters) {
        release_params(data->parameters);
        data->parameters = 0;
    }
    if (data->environment) {
        release_params(data->environment);
        data->environment = 0;
    }
}

static void release_temp_files(void *scope, struct MyData *data) {
    if (!data) {
        return;
    }
    if (data->stdout) {
        if (scope) {
            read_file_to(scope, "Result", data->stdout, setStringValue);
        }
        unlink(data->stdout);
        debug_free(data->stdout, "tmp_filename");
        data->stdout = 0;
    }
    if (data->stderr) {
        if (scope) {
            read_file_to(scope, "Errors", data->stderr, setStringValue);
        }
        unlink(data->stderr);
        debug_free(data->stderr, "tmp_filename");
        data->stderr = 0;
    }
}

static void destroy_instance(void *scope, struct MyData *data, char *current) {
    release_run_resources(data);
    release_temp_files(scope, data);
    if (scope) {
        setInstanceData(scope, 0);
    }
    if (current) {
        debug_free(current, "current");
    }
    if (data) {
        debug_free(data, "data");
    }
}

int exec_command(void *scope) {
    struct MyData *data = (struct MyData *)getInstanceData(scope);
    char *cmd;
    char *current = 0;

    if (!data) {
        data = (struct MyData *)debug_malloc(sizeof(struct MyData), "data");
        memset(data, 0, sizeof(struct MyData));
        setInstanceData(scope, data);
        if (!getIntValue(scope, "CommandStatus", &data->cmd_status)) {
            log_message(scope, "SYSTEMEXEC CommandStatus property is not an integer");
            changeState(scope, "Error");
            destroy_instance(scope, data, 0);
            return PLUGIN_ERROR;
        }
    }

    current = getState(scope);
    did_alloc("current");

    if (current && strcmp(current, "Start") == 0 && data->child == 0) {
        cmd = getStringValue(scope, "Command");
        if (cmd && *cmd) {
            did_alloc("cmd");
            if (!changeState(scope, "Running")) {
                debug_free(cmd, "cmd");
                goto CommandFinished;
            }
            data->parameters = split_string(cmd);
            data->environment = copy_environment();
            debug_free(cmd, "cmd");

            if (!data->parameters || !data->parameters[0] || !data->parameters[0][0]) {
                setIntValue(scope, "CommandStatus", EINVAL);
                changeState(scope, "Error");
                goto CommandFinished;
            }

            data->stdout = new_temp_filename("/tmp/sysexec_o_", ".txt");
            data->stderr = new_temp_filename("/tmp/sysexec_e_", ".txt");

            int child;
            if ((child = fork()) == -1) {
                perror("fork");
                setIntValue(scope, "CommandStatus", errno);
                changeState(scope, "Error");
                data->child = 0;
                goto CommandFinished;
            }
            else if (child == 0) { /* child */
                int out_fd = open(data->stdout, O_CREAT | O_TRUNC | O_WRONLY, 0640);
                if (out_fd == -1) {
                    perror("create stdout file");
                    _exit(3);
                }
                int err_fd = open(data->stderr, O_CREAT | O_TRUNC | O_WRONLY, 0640);
                if (err_fd == -1) {
                    perror("create stderr file");
                    _exit(3);
                }
                if (dup2(out_fd, 1) == -1 || dup2(err_fd, 2) == -1) {
                    perror("dup2");
                    _exit(3);
                }
                close(out_fd);
                close(err_fd);
                int res = execve(data->parameters[0], data->parameters, data->environment);
                if (res == -1) {
                    perror("execve");
                    release_params(data->parameters);
                    release_params(data->environment);
                    _exit(2);
                }
            }
            else {
                data->child = child;
            }
            debug_free(current, "current");
            return PLUGIN_COMPLETED; /* child still running */
        }
        else { /* empty or missing Command property in scope */
            /* getStringValue() returns strdup(); track free for debug_malloc balance */
            if (cmd) {
                did_alloc("cmd");
                debug_free(cmd, "cmd");
            }
            changeState(scope, "Error");
            data->child = 0;
            /* Keep instance allocated so a later successful start can reuse it;
             * do not free data while still registered on scope (historic UAF). */
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }
    }
    else if (data->child && current && strcmp(current, "Running") == 0) {
        int stat = 0;
        int err = waitpid(data->child, &stat, WNOHANG);
        if (err == -1) {
            if (errno == ECHILD) {
                /* Child already reaped; treat as completed with best-effort status. */
                if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
                    setIntValue(scope, "CommandStatus", 0);
                    changeState(scope, "Done");
                }
                else if (WIFEXITED(stat)) {
                    setIntValue(scope, "CommandStatus", WEXITSTATUS(stat));
                    changeState(scope, "Error");
                }
                else {
                    setIntValue(scope, "CommandStatus", errno);
                    changeState(scope, "Error");
                }
                data->child = 0;
                goto CommandFinished;
            }
            perror("waitpid");
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }
        else if (err == 0) {
            debug_free(current, "current");
            return PLUGIN_COMPLETED; // child still running
        }
        /* err == pid: child status available in stat */
        if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
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
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }
        debug_free(current, "current");
        return PLUGIN_COMPLETED;
    CommandFinished:
        release_run_resources(data);
        release_temp_files(scope, data);
        /* Drop instance so the next Start re-inits cleanly (matches re-start
         * WITHIN Done / Error on the SYSTEMEXEC machine). */
        setInstanceData(scope, 0);
        debug_free(current, "current");
        debug_free(data, "data");
        return PLUGIN_COMPLETED;
    }

    debug_free(current, "current");
    return PLUGIN_COMPLETED;
}
