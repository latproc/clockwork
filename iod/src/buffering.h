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
void addSample(struct CircularBuffer *buf, double pos, long time);
double rate(struct CircularBuffer *buf);
int size(struct CircularBuffer *buf);
int length(struct CircularBuffer *buf);
double average(struct CircularBuffer *buf);
double sum(struct CircularBuffer *buf);

#endif
