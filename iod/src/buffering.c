#include "buffering.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct CircularBuffer *createBuffer(int size) {
    if (size <= 0) {
        return 0;
    }
    struct CircularBuffer *buf = (struct CircularBuffer *)malloc(sizeof(struct CircularBuffer));
    buf->bufsize = size;
    buf->front = -1;
    buf->back = -1;
    buf->values = (double *)malloc(sizeof(double) * size);
    buf->times = (uint64_t *)malloc(sizeof(uint64_t) * size);
    buf->total = 0;
    return buf;
}

void destroyBuffer(struct CircularBuffer *buf) {
    free(buf->values);
    free(buf->times);
    free(buf);
}

int bufferIndexFor(struct CircularBuffer *buf, int i) {
    int l = bufferLength(buf);
    /*  it is a non recoverable error to access the buffer without
        adding a value */
    if (l == 0) {
        assert(0);
        abort();
    }

    if (i >= l) {
        return buf->back; /* looking past the end of the buffer */
    }
    return (buf->front + buf->bufsize - i) % buf->bufsize;
}

void addSample(struct CircularBuffer *buf, long time, double val) {
    buf->front = (buf->front + 1) % buf->bufsize;
    if (buf->front == buf->back) {
        buf->total -= buf->values[buf->front];
    }
    if (buf->front == buf->back || buf->back == -1) {
        buf->back = (buf->back + 1) % buf->bufsize;
    }
    buf->total += val;
    buf->values[buf->front] = val;
    buf->times[buf->front] = time;
}

void setBufferValue(struct CircularBuffer *buf, double val) { buf->values[buf->front] = val; }

void addSampleDebug(struct CircularBuffer *buf, long time, double val) {
    addSample(buf, val, time);
    printf("buffer added: %5.2f, %ld at %d\n", val, time, buf->front);
}

double rate(struct CircularBuffer *buf, int n) {
    if (buf->front == buf->back || buf->back == -1) {
        return 0.0f;
    }
    int idx = bufferIndexFor(buf, n);
    double v1 = buf->values[idx], v2 = buf->values[buf->front];
    double t1 = buf->times[idx], t2 = buf->times[buf->front];
    double ds = v2 - v1;
    double dt = t2 - t1;
    if (dt == 0) {
        return 0;
    }
    return ds / dt;
}

int findMovement(struct CircularBuffer *buf, double amount, int max_len) {
    /* reading the buffer without entering data is a non-recoverable error */
    int l = bufferLength(buf);
    if (l == 0) {
        assert(0);
        abort();
    }
    int n = 0;
    int idx = buf->front;
    double current = buf->values[idx];

    while (idx != buf->back && n < max_len) {
        idx--;
        if (idx == -1) {
            idx = buf->bufsize - 1;
        }
        ++n;
        if (fabs(current - buf->values[idx]) >= amount) {
            return n;
        }
    }
    return n;
}

double rateDebug(struct CircularBuffer *buf) {
    if (buf->front == buf->back || buf->back == -1) {
        return 0.0f;
    }
    double v1 = buf->values[buf->back], v2 = buf->values[buf->front];
    double t1 = buf->times[buf->back], t2 = buf->times[buf->front];
    double ds = v2 - v1;
    double dt = t2 - t1;
    if (dt == 0) {
        printf("error calculating rate: (%5.2f - %5.2f)/(%5.2f - %5.2f) = "
               "%5.2f / %5.2f \n",
               v2, v1, t2, t1, ds, dt);
    }
    else {
        printf("calculated rate: (%5.2f - %5.2f)/(%5.2f - %5.2f) = %5.2f / "
               "%5.2f = %5.2f\n",
               v2, v1, t2, t1, ds, dt, ds / dt);
    }
    if (dt == 0) {
        return 0;
    }
    return ds / dt;
}

double bufferAverage(struct CircularBuffer *buf, int n) {
    int l = bufferLength(buf);
    if (n > l) {
        n = l;
    }
    return (n == 0) ? n : bufferSum(buf, n) / n;
}

double bufferStddev(struct CircularBuffer *buf, int n) {
    double res = 0.0;
    int i = bufferLength(buf);
    if (i > n) {
        i = n;
    }
    if (n > i) {
        n = i;
    }
    if (n <= 1) {
        return 0.0;
    }
    {
        double avg = bufferAverage(buf, n);
        do {
            double val;
            i--;
            val = getBufferValue(buf, i) - avg;
            res = res + val * val;
        } while (i > 0);
    }
    return sqrt(res / (double)(n - 1));
}

double getBufferValue(struct CircularBuffer *buf, int n) {
    int idx = bufferIndexFor(buf, n);
    return buf->values[idx];
}

long getBufferTime(struct CircularBuffer *buf, int n) {
    int idx = bufferIndexFor(buf, n);
    return buf->times[idx];
}

double getBufferValueAt(struct CircularBuffer *buf, unsigned long t) {
    int n = bufferLength(buf) - 1;
    int idx = bufferIndexFor(buf, n);

    if (buf->times[idx] >= t) {
        return buf->values[idx];
    }
    if (t >= buf->times[buf->front]) {
        return buf->values[buf->front];
    }
    int nxt = (idx + 1) % buf->bufsize;
    while (buf->times[nxt] < t) {
        idx = nxt;
        nxt = (idx + 1) % buf->bufsize;
    }
    double dt = buf->times[nxt] - buf->times[idx];
    double scale = (double)(t - buf->times[idx]) / dt;
    return buf->values[idx] + scale * (buf->values[nxt] - buf->values[idx]);
}

double bufferSum(struct CircularBuffer *buf, int n) {
    //return buf->total;
    int i = bufferLength(buf);
    if (i > n) {
        i = n;
    }
    double tot = 0.0;
    while (i-- > 0) {
        tot = tot + getBufferValue(buf, i);
    }
    return tot;
}

int size(struct CircularBuffer *buf) { return buf->bufsize; }

unsigned int bufferLength(struct CircularBuffer *buf) {
    if (buf->front == -1) {
        return 0;
    }
    return (buf->front - buf->back + buf->bufsize) % buf->bufsize + 1;
}

double getTime(struct CircularBuffer *buf, int n) {
    return buf->times[(buf->front + buf->bufsize - n) % buf->bufsize];
}

double slope(struct CircularBuffer *buf) {
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0;
    double sumXsquared = 0.0, sumYsquared = 0.0;
    int n = bufferLength(buf) - 1;
    double t0 = getTime(buf, n);
    int i = 0;
    for (i = n - 1; i > 0; i--) {
        double y = getBufferValue(buf, i) - getBufferValue(buf, n); // degrees
        double x = getTime(buf, i) - t0;
        sumX += x;
        sumY += y;
        sumXsquared += x * x;
        sumYsquared += y * y;
        sumXY += x * y;
    }
    double denom = (double)n * sumXsquared - sumX * sumX;

    double m = 0.0;
    if (denom != 0.0) {
        m = ((double)n * sumXY - sumX * sumY) / denom;
    }

    //double c = (sumXsquared * sumY - sumXY * sumX) / denom;
    return m;
}

double savitsky_golay_filter(struct CircularBuffer *buf, unsigned int filter_len,
                             double *coefficients, float normal) {
    if (bufferLength(buf) < buf->bufsize || filter_len > buf->bufsize) {
        return getBufferValue(buf, 0);
    }
    double sum = 0;
    for (unsigned int i = 0; i < filter_len; i++) {
        sum += getBufferValue(buf, i) * coefficients[filter_len - i - 1];
    }
    return sum / normal;
}

#ifdef TESTING

#ifdef __cplusplus
extern "C" {
#endif

int failures = 0;

void fail(int test) {
    ++failures;
    printf("test %d failed\n", test);
}

int run_buffer_tests(int argc, const char *argv[]) {
    double (*sum)(struct CircularBuffer * buf, int n) = bufferSum;
    double (*average)(struct CircularBuffer * buf, int n) = bufferAverage;

    int tests = 0;
    int test_buffer_size = 4;
    struct CircularBuffer *mybuf = createBuffer(test_buffer_size);
    int i = 0;

    for (i = 0; i < 10; ++i) {
        addSample(mybuf, i, i);
    }
    for (i = 0; i < 10; ++i) {
        printf("index for %d: %d\n", i, bufferIndexFor(mybuf, i));
    }
    ++tests;
    if (rate(mybuf, 6) != 1.0) {
        fail(tests);
        printf("rate returned %.3lf\n", rate(mybuf, 4));
    }

    for (i = 0; i < 10; ++i) {
        addSample(mybuf, i, 1.5 * i);
    }
    ++tests;
    if (rate(mybuf, 8) != 1.5) {
        fail(tests);
        printf("rate returned %.3lf\n", rate(mybuf, 4));
    }
    ++tests;
    if (slope(mybuf) != 1.5) {
        fail(tests);
    }
    ++tests;
    if (bufferLength(mybuf) != test_buffer_size) {
        fail(tests);
    }
    ++tests;
    if (sum(mybuf, size(mybuf)) / test_buffer_size != average(mybuf, size(mybuf))) {
        fail(tests);
    }
    if (test_buffer_size == 4) {
        /* this test only works if the buffer is of bufferLength 4 */
        ++tests;
        if (sum(mybuf, size(mybuf)) != (6 + 7 + 8 + 9) * 1.5) {
            fail(tests);
        }
    }

    /// negative numbers
    destroyBuffer(mybuf);
    mybuf = createBuffer(test_buffer_size);
    long ave = average(mybuf, test_buffer_size);
    //printf("%ld\n",ave);
    addSample(mybuf, 10000, 1);
    //printf("%ld\n",ave);
    addSample(mybuf, 10040, -20);
    //printf("%ld\n",ave);
    for (i = 0; i < 10; ++i) {
        addSample(mybuf, i, -1.5 * i);
    }
    ++tests;
    if (rate(mybuf, size(mybuf)) != -1.5) {
        fail(tests);
        printf("unexpected rate: %lf expected -1.5\n", rate(mybuf, size(mybuf)));
    }

    i = 0;
    while (i < test_buffer_size) {
        addSample(mybuf, i, random() % 5000);
        ++i;
    }
    while (i < 8) {
        addSample(mybuf, i, 0);
        ++i;
    }
    ++tests;
    if (fabs(bufferAverage(mybuf, size(mybuf))) > 1.0E-4) {
        fail(tests);
        printf("unexpected average: %lf\n", average(mybuf, size(mybuf)));
    }
    ++tests;
    if (bufferSum(mybuf, size(mybuf)) != 0.0) {
        fail(tests);
        printf("unexpected sum: %lf\n", bufferSum(mybuf, size(mybuf)));
    }

    destroyBuffer(mybuf);

    /* test whether findMovement correctly finds a net movememnt */
    ++tests;
    mybuf = createBuffer(20);
    addSample(mybuf, 0, 2200);
    addSample(mybuf, 1, 2200);
    for (i = 2; i < 10; ++i) {
        addSample(mybuf, i, 2000);
    }
    for (; i < 20; ++i) {
        addSample(mybuf, i, 2020);
    }
    {
        int n;
        if ((n = findMovement(mybuf, 0.0f, 10)) != 10) {
            fail(tests);
            printf("findMovement(..,10) expected 10, got %d\n", n);
        }
        ++tests;
        for (; i < 28; ++i) {
            addSample(mybuf, i, 2020);
        }
        if ((n = findMovement(mybuf, 0.0f, 100)) != 18) {
            /*  2200,2200,2000,2000,2000,2000,2000,2000,2000,2000,
                2020,2020,2020,2020,2020,2020,2020,2020,2020,2020 */
            fail(tests);
            printf("findMovement(..,100) expected 18, got %d\n", n);
        }
    }
    destroyBuffer(mybuf);

    printf("tests:\t%d\nfailures:\t%d\n", tests, failures);

    /*
        generate a sign curve and calculate the slope using
        the rate() and slope() functions for comparison purposes
    */
    /*
        mybuf = createBuffer(4);
        for (i=0; i<70; i++) {
        double x = i/4.0;
        double y = sin( x );
        addSample(mybuf, i, y);
        if (i>mybuf->bufsize) printf("%d,%lf,%lf,%lf\n",i, cos(x)/4.0, rate(mybuf), slope(mybuf));
        }
        destroyBuffer(mybuf);
    */

    mybuf = createBuffer(6);
    for (i = 0; i < 6; ++i) {
        double x = i * 22 + 10;
        double y = i * 20 + 30;
        addSample(mybuf, x, y);
    }
    for (i = 10; i < 300; i += 10) {
        printf("%4ld\t%8.3f\n", (long)i, getBufferValueAt(mybuf, i));
    }
    return failures;
}
#ifdef __cplusplus
}
#endif

#endif
