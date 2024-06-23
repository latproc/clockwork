#ifndef __SPLIT_STRING_H__
#define __SPLIT_STRING_H__

#if __cplusplus
extern "C" {
#endif

int mallocs_remaining(void);
char **split_string(const char *str);
void display_params(char *argv[]);
void release_params(char *argv[]);

#if __cplusplus
}
#endif

#endif
