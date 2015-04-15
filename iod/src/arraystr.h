/* convert between an array of longs and a string and back again 

  written by: Martin Leadbeater 2015
*/

#ifndef __ARRAYSTR_H__
#define __ARRAYSTR_H__

char *longArrayToString(int n, long *values);
int stringToLongArray(const char *str, int n, long *values);

#endif
