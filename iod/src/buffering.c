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
	buf->total = 0;
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
	return (n==0) ? n : bufferSum(buf) / n;
}

double getBufferValue(struct CircularBuffer *buf, int n) {
	return buf->values[ (buf->front + buf->bufsize - n) % buf->bufsize];
}

double bufferSum(struct CircularBuffer *buf) {
	return buf->total;
	int i = length(buf);
	double tot = 0.0;
	while (i) {
		i--;
		tot = tot + getBufferValue(buf, i);
	}
	return tot;
}

int size(struct CircularBuffer *buf) {
    return buf->bufsize;
}

int length(struct CircularBuffer *buf) {
    if (buf->front == -1) return 0;
    return (buf->front - buf->back + buf->bufsize) % buf->bufsize + 1;
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
	double (*sum)(struct CircularBuffer *buf) = bufferSum;
	double (*average)(struct CircularBuffer *buf) = bufferAverage;
	
    int tests = 0;
	int test_buffer_size = 4;
    struct CircularBuffer *mybuf = createBuffer(test_buffer_size);
    int i = 0;

    for (i=0; i<10; ++i) addSample(mybuf, i, i);
    ++tests; if (rate(mybuf) != 1.0) { fail(tests); }

    for (i=0; i<10; ++i) addSample(mybuf, i, 1.5*i);
    ++tests; if (rate(mybuf) != 1.5) { fail(tests); }
    ++tests; if (slope(mybuf) != 1.5) { fail(tests); }
	++tests; if (length(mybuf) != test_buffer_size) { fail(tests); }
	++tests; if (sum(mybuf) / test_buffer_size != average(mybuf)) { fail(tests); }
	if (test_buffer_size == 4) { /* this test only works if the buffer is of length 4 */
		++tests; if (sum(mybuf) != (6 + 7 + 8 + 9)*1.5) { fail(tests); }
	}

	/// negative numbers
	destroyBuffer(mybuf);
    mybuf = createBuffer(test_buffer_size);
	long ave = average(mybuf);
	printf("%ld\n",ave);
	addSample(mybuf,10000,1);
	printf("%ld\n",ave);
	addSample(mybuf,10040,-20);
	printf("%ld\n",ave);
    for (i=0; i<10; ++i) addSample(mybuf, i, -1.5*i);
    ++tests; if (rate(mybuf) != -1.5) { fail(tests); printf("unexpected rate: %lf\n", rate(mybuf)); }

	i=0;
	while (i<test_buffer_size) { addSample(mybuf, i, random()%5000); ++i; }
	while (i<8) { addSample(mybuf, i, 0); ++i; }
	++tests; if (bufferAverage(mybuf) != 0.0) {fail(tests); printf("unexpected average: %lf\n", average(mybuf));}
	++tests; if (bufferSum(mybuf) != 0.0) {fail(tests); printf("unexpected sum: %lf\n", bufferSum(mybuf));}
	printf("tests:\t%d\nfailures:\t%d\n", tests, failures );

	destroyBuffer(mybuf);

	return 0;
	/* 
		generate a sign curve and calculate the slope using
		the rate() and slope() functions for comparison purposes
	*/
    mybuf = createBuffer(4);
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
