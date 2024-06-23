#include <copy_environment.h>
#include <stdlib.h>
#include <string.h>

char **copy_environment(void) {
    extern char **environ;
    char **curr = environ;
    char **result;
    char **out;
    int count = 0;
    while (*curr++)
        count++;
    count++;
    result = (char **)malloc(count * sizeof(char *));
    out = result;
    curr = environ;

    while (*curr)
        *out++ = strdup(*curr++);
    *out = NULL;
    return result;
}
