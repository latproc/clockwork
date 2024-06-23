#include <ctype.h>
#include <errno.h>
#include <split_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int malloc_count = 0;
static int free_count = 0;

static void *my_malloc(size_t size) {
    ++malloc_count;
    return malloc(size);
}

static char *my_strdup(char *str) {
    ++malloc_count;
    return strdup(str);
}

static void my_free(void *block) {
    ++free_count;
    free(block);
}

int mallocs_remaining(void) { return malloc_count - free_count; }

char **split_string(const char *str) {
    enum states { START, IN, IN_QUOTE, IN_DBLQUOTE, BETWEEN };
    enum toks { CHAR, SPC, TERM };
    int QUOTE = 0x27;
    int DBLQUOTE = '"';

    if (str == NULL) {
        return NULL;
    }

    int state = START;
    int tok;
    const char *p = str;

    /* buffer to collect individual parameters into */

    /* start bufsize with a reasonable guess at the length of individual
	    parameters within the string this value will grow it turns out to be too small
	*/
    int bufsize = 30;
    char *buf = my_malloc(bufsize);
    char *out = buf;

    char **params = my_malloc(50 * sizeof(char *)); /* TBD might need extra parameters here */
    memset(params, 0, 50 * sizeof(char *));
    int cur_param = 0;
    char **result = NULL;

    while (*p) {
        if (state == IN_QUOTE) {
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
        else if (!isspace(*p))
            tok = CHAR;
        else
            tok = SPC;

        switch (state) {
        case START:
            if (*p == QUOTE)
                state = IN_QUOTE;
            else if (*p == DBLQUOTE)
                state = IN_DBLQUOTE;
            else if (tok == CHAR) {
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
                params[cur_param] = my_strdup(buf);
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
            else if (tok == CHAR) {
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
        if ((out - buf) == bufsize) {
            /* make more space for parameters */
            char *saved = buf;
            buf = my_malloc(bufsize + 128);
            memcpy(buf, saved, bufsize);
            out = buf + bufsize;
            bufsize += 128;
            my_free(saved);
        }
        p++;
    }
    *out = 0;
    if (state == IN) {
        params[cur_param] = my_strdup(buf);
        cur_param++;
    }
    else if (state == IN_QUOTE || state == IN_DBLQUOTE) {
        printf("Warning: unterminated string '%s' is ignored while parsing: '%s'\n", buf, str);
    }
    result = (char **)my_malloc((cur_param + 1) * sizeof(char *));
    result[cur_param] = NULL;
    int i = 0;
    for (i = 0; i < cur_param; ++i)
        result[i] = params[i];
    my_free(params);
    my_free(buf);
    return result;
}

void display_params(char *argv[]) {
    char **curr = argv;
    char *val = *curr;
    while (val) {
        printf("%s:", val);
        val = *(++curr);
    }
    printf("\n");
    fflush(stdout);
}

void release_params(char *argv[]) {
    char **curr = argv;
    char *val = *curr;
    while (val) {
        my_free(val);
        val = *(++curr);
    }
    my_free(argv);
}
