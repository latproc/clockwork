#ifndef __cmdline_h__
#define __cmdline_h__ 1

#include <list>
#include "value.h"

void yyerror(const char *str);
#define YY_INPUT(buf,result,max_size) result = get_input(buf, max_size);

extern int get_input(char *buf, unsigned int size);

#ifndef _MAIN_
extern std::list<Value> params;
extern void process_command(std::list<Value> &);
#endif
#endif
