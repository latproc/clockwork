# This plugin attempts to execute the command set in its Command property
# when it is in the Start state.
# While the command is running the machine stays in the Running state until
# eventually it moved to Error or Done at which time CommandStatus reflects
# the return result from the command (0 for no error)

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
}

%BEGIN_PLUGIN
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <Plugin.h>
#include <sys/stat.h>
#include <fcntl.h>

struct MyData {
	char **environment;
	char **parameters;
	const long *cmd_status;
	int child;
  char *stdout;
  char *stderr;
};

char ** split_string(const char *str)
{
	enum states { START, IN, IN_QUOTE, IN_DBLQUOTE, BETWEEN };
	enum toks { CHAR, SPC, TERM};
	int QUOTE = 0x27;
	int DBLQUOTE = '"';

	int state = START;
	int tok;
	const char *p = str;

	/* buffer to collect individual parameters into */

	/* start bufsize with a reasonable guess at the length of individual
	    parameters within the string this value will grow it turns out to be too small
	*/
	int bufsize = 30;
	char *buf = malloc(bufsize);
	char *out = buf;

	char **params = malloc( 50 * sizeof(char*) ); /* TBD might need extra parameters here */
	memset(params, 0, 50 * sizeof(char*));
	int cur_param = 0;
	char **result = NULL;

	while (*p)
	{
		if (state == IN_QUOTE)
		{
		    if (*p != QUOTE)
				tok = CHAR;
			else
				tok = TERM;
		}
		else if (state == IN_DBLQUOTE) {
			if (*p != DBLQUOTE)
				tok = CHAR;
			else
				tok = TERM;
		}
		else if (!isspace(*p)  )
			tok = CHAR;
		else
			tok = SPC;

		switch (state)
		{
			case START:
				if (*p == QUOTE)
					state = IN_QUOTE;
				else if (*p == DBLQUOTE)
					state = IN_DBLQUOTE;
				else if (tok == CHAR)
				{
					*out++ = *p;
					state = IN;
				}
				else if (tok == SPC)
					state = BETWEEN;
				break;
			case IN:
				if (*p == QUOTE)
					state = IN_QUOTE;
				else if (*p == DBLQUOTE)
					state = IN_DBLQUOTE;
			    else if (tok == CHAR)
					*out++ = *p;
				else if (tok == SPC) {
					/* finished a token */
					*out = 0;
					params[cur_param] = strdup (buf);
					cur_param++;
					out = buf;
					state = BETWEEN;
				}
				break;
			case BETWEEN:
			    if (*p == QUOTE)
					state = IN_QUOTE;
				else if (*p == DBLQUOTE)
					state = IN_DBLQUOTE;
				else if (tok == CHAR)
				{
					*out++ = *p;
					state = IN;
				}
				break;
			case IN_QUOTE:
				if (tok == TERM)
					state = IN;
				else if (tok == CHAR)
					*out++ = *p;
				break;
			case IN_DBLQUOTE:
				if (tok == TERM)
					state = IN;
				else if (tok == CHAR)
					*out++ = *p;
			    break;
			default:
			    printf("Error: unknown state %d when parsing a string.\n", state);
		}
		if ( (out - buf) == bufsize )
		{
			/* make more space for parameters */
			char *saved = buf;
			buf = malloc(bufsize + 128);
			memcpy(buf, saved, bufsize);
			out = buf + bufsize;
			bufsize += 128;
			free(saved);
		}
		p++;
	}
	*out = 0;
	if (state == IN) {
		params[cur_param] = strdup (buf);
		cur_param++;
	}
	else if (state == IN_QUOTE || state == IN_DBLQUOTE)
	{
		printf("Warning: unterminated string '%s' is ignored while parsing: '%s'\n", buf, str);
	}
	result = (char **)malloc( (cur_param + 1) * sizeof(char *));
	result[cur_param] = NULL;
	int i = 0;
	for (i=0; i<cur_param; ++i) result[i] = params[i];
	free(params);
	free(buf);
	return result;
}

void display_params(char *argv[])
{
	char **curr = argv;
	char *val = *curr;
	int count = 0;
	while (val)
	{
		printf("%s:", val);
		val = *(++curr);
		count++;
	}
	printf("\n");
	fflush(stdout);
}

void release_params(char *argv[])
{
	char **curr = argv;
	char *val = *curr;
	while (val)
	{
		free(val);
		val = *(++curr);
	}
	free(argv);
}


char **copy_environment()
{
  extern char** environ;
  char **curr = environ;
  char **result;
  char **out;
  int count = 0;
  while (*curr++) count++;
  count++;
  result = (char**) malloc(count * sizeof(char*));
  out = result;
  curr = environ;

  while (*curr)
    *out++ = strdup(*curr++);
  *out = NULL;
  return result;
}

char * new_temp_filename(const char *prefix, const char *suffix)
{
  char tmp_filename[80];
  int res;

  do {
    struct stat fs;
    sprintf(tmp_filename, "%s%04ld%s", prefix, random() % 10000, suffix);
    res = stat(tmp_filename, &fs);
  }
  while (res != -1);
  return strdup(tmp_filename);
}

static void read_file_to(void *scope, const char *property, const char *filename) {
  struct stat fs;
  int res = stat(filename, &fs);
  if (res == 0) {
    const size_t bufsize = fs.st_size+1;
    char *buf = malloc(bufsize);
    FILE *f = fopen(filename, "r");
    if (f) {
      size_t nread = fread(buf, 1, bufsize, f);
      if (nread != bufsize-1) {
        fprintf(stderr, "read %ld bytes, expected %ld\n", nread, bufsize);
      }
      if (ferror(f)) {
        fprintf(stderr, "error during read of %s: %d\n", filename, ferror(f));
        clearerr(f);
      }
      buf[nread] = 0;
      setStringValue(scope, property, buf);
    }
    free(buf);
  }
}

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	char *cmd;
	char *current = 0;
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		data->parameters = 0;
		data->environment = 0;
		data->cmd_status = 0;
		data->child = 0;
    if (!getIntValue(scope, "CommandStatus", &data->cmd_status)) {
      log_message(scope, "SystemExec CommandStatus property is not an integer");
      goto plugin_init_error;
    }

		goto continue_plugin;
plugin_init_error:
		setInstanceData(scope, 0);
		free(data);
		return PLUGIN_ERROR;
	}

continue_plugin:

	current = getState(scope);
	if (current && strcmp(current, "Start") == 0 && data->child == 0 ) {
		cmd = getStringValue(scope, "Command");
		if (cmd && *cmd) {
			if (!changeState(scope, "Running")) {
				goto CommandFinished;
      }
			data->parameters = split_string(cmd);
			data->environment = copy_environment();

      data->stdout = new_temp_filename("/tmp/sysexec_o_", ".txt");
      data->stderr = new_temp_filename("/tmp/sysexec_e_", ".txt");

			int child;
			if ( (child = fork()) == -1 ) {
				perror("fork");
				setIntValue(scope, "CommandStatus", errno);
				changeState(scope, "Error");
				data->child = 0;
				goto CommandFinished;
			}
			else if (child == 0) {  /* child */
          // Redirect stdout and stderr
          int out_fd = open(data->stdout, O_CREAT | O_TRUNC | O_WRONLY, 0640);
          if (out_fd == -1) {
            perror("create stdout");
            exit(3);
          }
          int err_fd = open(data->stderr, O_CREAT | O_TRUNC | O_WRONLY, 0640);
          if (err_fd == -1) {
            perror("create stderr");
            exit(3);
          }
          close(1);
          dup(out_fd);
          close(out_fd);
          close(2);
          dup(err_fd);
          close(err_fd);
          int res = execve(data->parameters[0], data->parameters, data->environment);
          if (res == -1)
          {
            perror("execve");
            display_params(data->parameters);
						_exit(2);
          }
			}
			else
				data->child = child;
			return PLUGIN_COMPLETED; // child still running
		}
		else {
			changeState(scope, "Error");
			data->child = 0;
			return PLUGIN_COMPLETED; // child still running
		}
	}
	else if (data->child && current && strcmp(current, "Running") == 0 ) {
		int stat = 0;
		int err = waitpid(data->child, &stat, WNOHANG);
		if (err == -1)
		{
			perror("waitpid");
			if (errno == ECHILD)
				printf("child exited %d (stat = %d)\n", WEXITSTATUS(stat), stat);
		}
		else if (err == 0) {
			return PLUGIN_COMPLETED; // child still running
		}
		if (stat == 0)
		{
			setIntValue(scope, "CommandStatus", 0);
			changeState(scope, "Done");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFEXITED(stat))
		{
			setIntValue(scope, "CommandStatus", WEXITSTATUS(stat));
			changeState(scope, "Error");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFSIGNALED(stat))
		{
			setIntValue(scope, "CommandStatus", WTERMSIG(stat));
			changeState(scope, "Error");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFSTOPPED(stat))
		{
			setIntValue(scope, "CommandStatus", WSTOPSIG(stat));
		}
		return PLUGIN_COMPLETED;
CommandFinished:
		if (data->parameters) { release_params(data->parameters); }
		if (data->environment) { release_params(data->environment); }
		data->parameters = 0;
		data->environment = 0;
    if (data->stdout) {
      read_file_to(scope, "Result", data->stdout);
      unlink(data->stdout); free(data->stdout); data->stdout = 0;
    }
    if (data->stderr) {
      read_file_to(scope, "Errors", data->stderr);
      unlink(data->stderr); free(data->stderr); data->stderr = 0;
    }
	}
	free(current);
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	return PLUGIN_COMPLETED;
}

%END_PLUGIN
