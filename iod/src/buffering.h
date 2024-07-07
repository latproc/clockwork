#ifndef __BUFFERING_H__
#define __BUFFERING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdlib.h>

struct CircularBuffer {
    unsigned int bufsize;
    int front;
    int back;
    double total;
    double *values;
    uint64_t *times;
};

struct CircularBuffer *createBuffer(int size);
void destroyBuffer(struct CircularBuffer *buf);
size_t size(struct CircularBuffer *buf);
unsigned int bufferLength(struct CircularBuffer *buf);

/* return the n-point moving average */
double bufferAverage(struct CircularBuffer *buf, size_t n);
double bufferStddev(struct CircularBuffer *buf, size_t n);
double bufferSum(struct CircularBuffer *buf, size_t n);
double getBufferValue(struct CircularBuffer *buf, size_t n);
void addSample(struct CircularBuffer *buf, long time, double val);
/* overwrite the last sample value */
void setBufferValue(struct CircularBuffer *buf, double val);

/*  seek back along the buffer to find the number of samples
    before a total movement of amount occurred, ignoring direction */
size_t findMovement(struct CircularBuffer *buf, double amount, size_t max_len);

double getBufferValueAt(struct CircularBuffer *buf,
                        unsigned long t); /* return an estimate of the value at time t */

/* return the current head of buffer, smoothed using the given coefficients */
double savitsky_golay_filter(struct CircularBuffer *buf, size_t filter_len, double *coefficients,
                             float normal);

/* calculate the rate of change by a direct method */
double rate(struct CircularBuffer *buf, size_t n);

/* calculate the rate of change using a least squares fit (slow) */
double slope(struct CircularBuffer *buf);
#ifdef __cplusplus
}
#endif

#endif
