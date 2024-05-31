#ifndef __FILTERING_H__
#define __FILTERING_H__

#include <boost/thread/recursive_mutex.hpp>

class Buffer {
  public:
    size_t BUFSIZE;
    int front;
    int back;
    double total_;
    virtual double getFloatAtOffset(ssize_t offset) const = 0;
    virtual double getFloatAtIndex(size_t idx) const = 0;
    double difference(size_t idx_a, size_t idx_b) const;
    double distance(size_t idx_a, size_t idx_b) const;
    double average(size_t n);
    double stddev(size_t n);
    ssize_t length() const;
    void reset();
    Buffer(size_t buf_size);
    virtual ~Buffer() {}

  private:
    Buffer(const Buffer &);
    Buffer &operator=(const Buffer &);

  protected:
    boost::recursive_mutex q_mutex;
};

class LongBuffer : public Buffer {
  public:
    long *buf;
    double getFloatAtOffset(ssize_t offset) const;
    double getFloatAtIndex(size_t idx) const;
    void append(long val);
    long get(size_t n) const;
    void set(size_t n, long value);
    LongBuffer(size_t buf_size) : Buffer(buf_size) { buf = new long[BUFSIZE]; }
    ~LongBuffer() { delete[] buf; }

  private:
    LongBuffer(const LongBuffer &);
    LongBuffer &operator=(const LongBuffer &);
};

class FloatBuffer : public Buffer {
  public:
    double *buf;
    double getFloatAtOffset(ssize_t offset) const;

    double getFloatAtIndex(size_t idx) const;
    void append(double val);
    double get(size_t n) const;
    void set(size_t n, double value);
    double slopeFromLeastSquaresFit(const LongBuffer &time_buf);
    double inner_product(double *coefficients, size_t num_coeff) const;
    double movingAverage(size_t n) const;
    FloatBuffer(size_t buf_size) : Buffer(buf_size) { buf = new double[BUFSIZE]; }
    ~FloatBuffer() { delete[] buf; }

  private:
    FloatBuffer(const FloatBuffer &);
    FloatBuffer &operator=(const FloatBuffer &);
};

class SampleBuffer : public Buffer {
  public:
    double *values;
    uint64_t *times;
    double getFloatAtOffset(ssize_t offset) const;
    double getFloatAtIndex(size_t idx) const;

    void append(double val, uint64_t time);
    void quickAppend(double val, uint64_t time);

    double rate() const; // returns dv/dt between the two sample positions

    SampleBuffer(size_t buf_size) : Buffer(buf_size) {
        values = new double[buf_size];
        times = new uint64_t[buf_size];
    }
    ~SampleBuffer() {
        delete[] values;
        delete[] times;
    }

  private:
    SampleBuffer(const SampleBuffer &);
    SampleBuffer &operator=(const SampleBuffer &);
};

class ButterworthFilter {
  public:
    ButterworthFilter(size_t num_c, double *c_coeff, size_t num_d, double *d_coeff)
        : num_c_coefficients(num_c), c_coefficients(c_coeff), signal_buf(num_c + 1),
          num_d_coefficients(num_d), d_coefficients(d_coeff), filtered_buf(num_d + 1) {}
    float filter(float value);

  protected:
    size_t num_c_coefficients;
    double *c_coefficients;
    FloatBuffer signal_buf;
    size_t num_d_coefficients;
    double *d_coefficients;
    FloatBuffer filtered_buf;
};

#endif
