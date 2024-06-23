#ifndef __READ_FILE_H__
#define __READ_FILE_H__

typedef void (*setter)(void *, const char *, const char *);
void read_file_to(void *scope, const char *property, const char *filename, setter set_value);

#endif
