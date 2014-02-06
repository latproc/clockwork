#ifndef __FILTERING_H__
#define __FILTERING_H__

class Buffer
{
public:
    const int BUFSIZE;
    int front;
    int back;
    virtual float getFloatAtOffset(int offset) const = 0;
    virtual float getFloatAtIndex(int idx) const = 0;
    float difference(int idx_a, int idx_b) const;
    float distance(int idx_a, int idx_b) const;
    float average(int n);
    int length();
    Buffer(int buf_size);
private:
    Buffer(const Buffer &);
    Buffer &operator=(const Buffer&);
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
    LongBuffer(int buf_size) : Buffer(buf_size) { }
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
    FloatBuffer(int buf_size) : Buffer(buf_size) { }
    float slopeFromLeastSquaresFit(const LongBuffer &time_buf);
private:
    FloatBuffer(const FloatBuffer &);
    FloatBuffer &operator=(const FloatBuffer&);
};

#endif

