#ifndef __BUFFERING_H__
#define __BUFFERING_H__

#include <inttypes.h>
struct CircularBuffer {
    int bufsize;
    int front;
    int back;
    double total;
    double *values;
    uint64_t *times;
};

struct CircularBuffer *createBuffer(int size);
void destroyBuffer(struct CircularBuffer *buf);
void addSample(struct CircularBuffer *buf, long time, double pos);
int size(struct CircularBuffer *buf);
int length(struct CircularBuffer *buf);
double bufferAverage(struct CircularBuffer *buf);
double bufferSum(struct CircularBuffer *buf);
double getBufferVal(struct CircularBuffer *buf, int n); /* return the item added n items ago */

/* calculate the rate of change by a direct method */
double rate(struct CircularBuffer *buf);

/* calculate the rate of change using a least squares fit (slow) */
double rate(struct CircularBuffer *buf);

#endif
