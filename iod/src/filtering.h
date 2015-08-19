#ifndef __FILTERING_H__
#define __FILTERING_H__

#include <boost/thread.hpp>

class Buffer
{
public:
    const int BUFSIZE;
    int front;
    int back;
	double total_;
    virtual float getFloatAtOffset(int offset) const = 0;
    virtual float getFloatAtIndex(int idx) const = 0;
    float difference(int idx_a, int idx_b) const;
    float distance(int idx_a, int idx_b) const;
    float average(int n);
    int length();
    void reset();
    Buffer(int buf_size);
	virtual ~Buffer() { }
private:
    Buffer(const Buffer &);
    Buffer &operator=(const Buffer&);
protected:
    boost::recursive_mutex q_mutex;
};

class LongBuffer : public Buffer
{
public:
    long *buf;
    float getFloatAtOffset(int offset) const;
    float getFloatAtIndex(int idx) const;
    void append(long val);
    long get(unsigned int n) const;
    void set(unsigned int n, long value);
    LongBuffer(int buf_size) : Buffer(buf_size) { buf = new long[BUFSIZE]; }
	~LongBuffer() { delete[] buf; }
private:
    LongBuffer(const LongBuffer &);
    LongBuffer &operator=(const LongBuffer&);
};

class FloatBuffer : public Buffer
{
public:
    float *buf;
    float getFloatAtOffset(int offset) const;

    float getFloatAtIndex(int idx) const;
    void append( float val);
    float get(unsigned int n) const;
    void set(unsigned int n, float value);
    float slopeFromLeastSquaresFit(const LongBuffer &time_buf);
    FloatBuffer(int buf_size) : Buffer(buf_size) { buf = new float[BUFSIZE]; }
	~FloatBuffer() { delete[] buf; }
private:
    FloatBuffer(const FloatBuffer &);
    FloatBuffer &operator=(const FloatBuffer&);
};

class SampleBuffer : public Buffer {
public:
    float *values;
    uint64_t *times;
    float getFloatAtOffset(int offset) const;
    float getFloatAtIndex(int idx) const;
    
    void append( float val, uint64_t time);
    void quickAppend( float val, uint64_t time);
    
    float rate() const; // returns dv/dt between the two sample positions
    
    SampleBuffer(int buf_size) : Buffer(buf_size) { values = new float[buf_size]; times = new uint64_t[buf_size]; }
    ~SampleBuffer() { delete[] values; delete[] times; }

private:
    SampleBuffer(const SampleBuffer &);
    SampleBuffer &operator=(const SampleBuffer&);
};

#endif

