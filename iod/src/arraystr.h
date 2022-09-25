/*  convert between an array of longs and a string and back again

    written by: Martin Leadbeater 2015
*/

#pragma once

char *longArrayToString(int n, long *values);
int stringToLongArray(const char *str, int n, long *values);

/*  the following permit passing an array of doubles but
    note that the string will only include the whole part of the value
*/
char *doubleArrayToString(int n, double *values);
int stringToDoubleArray(const char *str, int n, double *values);
