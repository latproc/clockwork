#ifndef __BUFFERING_H__
#define __BUFFERING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
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
int size(struct CircularBuffer *buf);
unsigned int bufferLength(struct CircularBuffer *buf);
double bufferAverage(struct CircularBuffer *buf, int n);
double bufferStddev(struct CircularBuffer *buf, int n);
double bufferSum(struct CircularBuffer *buf, int n);
double getBufferValue(struct CircularBuffer *buf, int n);
void addSample(struct CircularBuffer *buf, long time, double val);
/* overwrite the last sample value */
void setBufferValue(struct CircularBuffer *buf, double val);

/* seek back along the buffer to find the number of samples 
    before a total movement of amount occurred, ignoring direction */
int findMovement(struct CircularBuffer *buf, double amount, int max_len);

double getBufferValueAt(struct CircularBuffer *buf, unsigned long t); /* return an estimate of the value at time t */

/* return the current head of buffer, smoothed using the given coefficients */
double savitsky_golay_filter(struct CircularBuffer *buf, unsigned int filter_len, double *coefficients, float normal );

/* return the n-point moving average */
double moving_average(struct CircularBuffer *buf, unsigned int n);

/* calculate the rate of change by a direct method */
double rate(struct CircularBuffer *buf, int n);

/* calculate the rate of change using a least squares fit (slow) */
double slope(struct CircularBuffer *buf);
#ifdef __cplusplus
}
#endif

#endif
