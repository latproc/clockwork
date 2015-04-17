#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include "buffering.h"
#ifdef TEST
#include <math.h>
#endif

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

void addSample(struct CircularBuffer *buf, long time, double val) {
  buf->front = (buf->front + 1) % buf->bufsize;
  if (buf->front == buf->back) buf->total -= buf->values[buf->front];
  if (buf->front == buf->back || buf->back == -1) buf->back = (buf->back + 1) % buf->bufsize;
  buf->total += val;
  buf->values[buf->front] = val;
  buf->times[buf->front] = time;
}

void addSampleDebug(struct CircularBuffer *buf, long time, double val) {
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

double bufferAverage(struct CircularBuffer *buf) {
	int n = length(buf);
	return (n==0) ? n : buf->total / n;
}

double bufferSum(struct CircularBuffer *buf) {
	return buf->total;
}

int size(struct CircularBuffer *buf) {
    return buf->bufsize;
}

int length(struct CircularBuffer *buf) {
    if (buf->front == -1) return 0;
    return (buf->front - buf->back + buf->bufsize) % buf->bufsize + 1;
}

double getBufferValue(struct CircularBuffer *buf, int n) {
	return buf->values[ (buf->front + buf->bufsize - n) % buf->bufsize];
}

double getTime(struct CircularBuffer *buf, int n) {
	return buf->times[ (buf->front + buf->bufsize - n) % buf->bufsize];
}

double slope(struct CircularBuffer *buf) {
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0;
    double sumXsquared = 0.0, sumYsquared = 0.0;
    int n = length(buf)-1;
    double t0 = getTime(buf, n);
		int i = 0;
    for (i = n-1; i>0; i--) {
        double y = getBufferValue(buf, i) - getBufferValue(buf, n); // degrees
        double x = getTime(buf, i) - t0;
        sumX += x; sumY += y; sumXsquared += x*x; sumYsquared += y*y; sumXY += x*y;
    }
    double denom = (double)n*sumXsquared - sumX*sumX;
	
    double m = 0.0;
	if (denom != 0.0) m  = ((double)n * sumXY - sumX*sumY) / denom;
	
    //double c = (sumXsquared * sumY - sumXY * sumX) / denom;
    return m;
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
    for (i=0; i<10; ++i) addSample(mybuf, i, 1.5*i);
    ++tests; if (rate(mybuf) != 1.5) { fail(tests); }
    ++tests; if (slope(mybuf) != 1.5) { fail(tests); }
	++tests; if (sum(mybuf) / 4 != average(mybuf)) { fail(tests); }
	++tests; if (sum(mybuf) != (6 + 7 + 8 + 9)*1.5) { fail(tests); }
	printf("tests:\t%d\nfailures:\t%d\n", tests, failures );

	destroyBuffer(mybuf);

	/* 
		generate a sign curve and calculate the slope using
		the rate() and slope() functions for comparison purposes
	*/
    mybuf = createBuffer(3);
	for (i=0; i<70; i++) {
		double x = i/4.0;
		double y = sin( x );
		addSample(mybuf, i, y);
		if (i>mybuf->bufsize) printf("%d,%lf,%lf,%lf\n",i, cos(x)/4.0, rate(mybuf), slope(mybuf));
	}

	destroyBuffer(mybuf);
    return 0;
}
#endif
