/* convert between an array of longs and a string and back again 

  Assumes ascii character set (or at least a single byte character set with 
	digit characters in sequence).

  written by: Martin Leadbeater 2015
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

char *longArrayToString(int n, long *values) {

	int buflen = 0;
	char *buf;
	buflen = n*12; 
	buf = (char *)malloc(buflen); 
	int pos = 0;
	int i=0;
	for (i=0; i<n; ++i) {
		int extra = snprintf(buf+pos, buflen-pos, "%ld", values[i]);
		if (extra<0) { perror("snprintf"); return buf; }
		pos += extra;
		if (extra+1 >= buflen) break;
		if (i+1<n) { buf[pos++] = ','; buf[pos] = 0; } /* add a delimiter */
	}
	return buf;
}

int stringToLongArray(const char *str, int n, long *values){
	int idx = 0;
	enum State { e_num, e_sep };
	enum State state = e_num;
	long val = 0;
	const char *p = str;
	char ch;
	while ( (ch = *p++) ) {
		if (idx>=n) break;
		if (state == e_num) {
			if (isdigit(ch)) val = val*10 + (ch-'0');
			else { state = e_sep; values[idx++] = val; val = 0;}
		}
		else if (state == e_sep) {
			if (isdigit(ch)) { state = e_num; val = (ch-'0'); }
		}
	}
	if (state == e_num) values[idx++] = val; 
	return idx;
}

#ifdef TEST
#include <time.h>

int main(int argc, char *argv[]) {

	long vals[8];
	int n = 8, i;
	srandom(time(0));
	for (i=0; i<n; ++i) vals[i] = random();
	char *str = longArrayToString(n, vals);
	printf("%s\n", str);

	long A[10];
	n =  stringToLongArray(str, 10, A);
	printf ("%d numbers found\n",  n);
	for (i=0; i<n; ++i) printf("%ld\n", A[i]);
	free(str);
	return 0;
}
#endif
