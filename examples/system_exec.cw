# This plugin attempts to execute the command set in its Command property 
# when it is in the Start state.
# While the command is running the machine stays in the Running state until
# eventually it moved to Error or Done at which time CommandStatus reflects
# the return result from the command (0 for no error)

SystemExec MACHINE {
	OPTION Command "";
	OPTION CommandStatus 0;
	PLUGIN "system_exec.so.1.0";
	
	Running STATE;
	Start STATE;
	Error STATE;
	Done STATE;
	Idle INITIAL;
	
	COMMAND start { SET SELF TO Start; }
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

struct MyData {
	char **environment;
	char **parameters;
	const long *cmd_status;
	int child;
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


PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	char *cmd;
	char *current;
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
			int x = changeState(scope, "Running");
			data->parameters = split_string(cmd);
			data->environment = copy_environment();

			int child;
			if ( (child = vfork()) == -1 ) {
				perror("vfork");
				setIntValue(scope, "CommandStatus", errno);
				x = changeState(scope, "Error");
				data->child = 0;
				goto CommandFinished;
			}
			else if (child == 0) {  /* child */
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
			int x = changeState(scope, "Error");
			data->child = 0;
			return PLUGIN_COMPLETED; // child still running
		}
	}
	else if (current && strcmp(current, "Running") == 0 ) { 
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
			int x = changeState(scope, "Done");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFEXITED(stat))
		{
			setIntValue(scope, "CommandStatus", WEXITSTATUS(stat));
			int x = changeState(scope, "Error");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFSIGNALED(stat))
		{
			setIntValue(scope, "CommandStatus", WTERMSIG(stat));
			int x = changeState(scope, "Error");
			data->child = 0;
			goto CommandFinished;
		}
		else if (WIFSTOPPED(stat))
		{
			setIntValue(scope, "CommandStatus", WSTOPSIG(stat));
		}
		return PLUGIN_COMPLETED;
CommandFinished:
		release_params(data->parameters);
		release_params(data->environment);
		data->parameters = 0;
		data->environment = 0;
		return PLUGIN_COMPLETED;
	}
	free(current);
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	return PLUGIN_COMPLETED;
}

%END_PLUGIN
