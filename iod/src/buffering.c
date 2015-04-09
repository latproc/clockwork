#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include "buffering.h"

struct CircularBuffer *createBuffer(int size)
{
    struct CircularBuffer *buf = (struct CircularBuffer *)malloc(sizeof(struct CircularBuffer));
    buf->bufsize = size;
    buf->front = -1;
    buf->back = -1;
    buf->values = (double*)malloc( sizeof(double) * size);
    buf->times = (uint64_t*)malloc( sizeof(long) * size);
        return buf;
}

void destroyBuffer(struct CircularBuffer *buf) {
    free(buf->values);
    free(buf->times);
    free(buf);
}

void addSample(struct CircularBuffer *buf, double val, long time) {
  buf->front = (buf->front + 1) % buf->bufsize;
  if (buf->front == buf->back) buf->total -= buf->values[buf->front];
  if (buf->front == buf->back || buf->back == -1) buf->back = (buf->back + 1) % buf->bufsize;
  buf->total += val;
  buf->values[buf->front] = val;
  buf->times[buf->front] = time;
}

void addSampleDebug(struct CircularBuffer *buf, double val, long time) {
	addSample(buf, val, time);
	printf("buffer added: %5.2f, %ld at %d\n", val, time, buf->front);
}

double rate(struct CircularBuffer *buf) {
    if (buf->front == buf->back || buf->back == -1) return 0.0f;
    double v1 = buf->values[buf->back], v2 = buf->values[buf->front];
    double t1 = buf->times[buf->back], t2 = buf->times[buf->front];
    double ds = v2-v1;
    double dt = t2-t1;
		if (dt == 0) return 0;
    return ds/dt;
}

double rateDebug(struct CircularBuffer *buf) {
    if (buf->front == buf->back || buf->back == -1) return 0.0f;
    double v1 = buf->values[buf->back], v2 = buf->values[buf->front];
    double t1 = buf->times[buf->back], t2 = buf->times[buf->front];
    double ds = v2-v1;
    double dt = t2-t1;
		if (dt == 0) 
			printf("error calculating rate: (%5.2f - %5.2f)/(%5.2f - %5.2f) = %5.2f / %5.2f \n", v2, v1, t2, t1, ds, dt);
		else
			printf("calculated rate: (%5.2f - %5.2f)/(%5.2f - %5.2f) = %5.2f / %5.2f = %5.2f\n", v2, v1, t2, t1, ds, dt, ds/dt);
		if (dt == 0) return 0;
    return ds/dt;
}

double average(struct CircularBuffer *buf) {
	int n = length(buf);
	return (n==0) ? n : buf->total / n;
}

double sum(struct CircularBuffer *buf) {
	return buf->total;
}

int size(struct CircularBuffer *buf) {
    return buf->bufsize;
}

int length(struct CircularBuffer *buf) {
    if (buf->front == -1) return 0;
    return (buf->front - buf->back + buf->bufsize) % buf->bufsize + 1;
}

#ifdef TEST

int failures = 0;

void fail(int test) {
	++failures;
	printf("test %d failed\n", test);
}

int main(int argc, const char *argv[]) {
    int tests = 0;
    struct CircularBuffer *mybuf = createBuffer(4);
    int i = 0;
    for (i=0; i<10; ++i) addSample(mybuf, i, i);
    ++tests; if (rate(mybuf) != 1.0) { fail(tests); }
    for (i=0; i<10; ++i) addSample(mybuf, 1.5*i, i);
    ++tests; if (rate(mybuf) != 1.5) { fail(tests); }
	++tests; if (sum(mybuf) / 4 != average(mybuf)) { fail(tests); }
	++tests; if (sum(mybuf) != (6 + 7 + 8 + 9)*1.5) { fail(tests); }
	printf("tests:\t%d\nfailures:\t%d\n", tests, failures );
	destroyBuffer(mybuf);
    return 0;
}
#endif
