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
    virtual double getFloatAtOffset(int offset) const = 0;
    virtual double getFloatAtIndex(int idx) const = 0;
    double difference(int idx_a, int idx_b) const;
    double distance(int idx_a, int idx_b) const;
    double average(int n);
    double stddev(int n);
    unsigned int length() const;
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
    double getFloatAtOffset(int offset) const;
    double getFloatAtIndex(int idx) const;
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
    double *buf;
    double getFloatAtOffset(int offset) const;

    double getFloatAtIndex(int idx) const;
    void append( double val);
    double get(unsigned int n) const;
    void set(unsigned int n, double value);
    double slopeFromLeastSquaresFit(const LongBuffer &time_buf);
    double inner_product(double *coefficients, unsigned int num_coeff ) const;
    double movingAverage(unsigned int n) const;
    FloatBuffer(int buf_size) : Buffer(buf_size) { buf = new double[BUFSIZE]; }
	~FloatBuffer() { delete[] buf; }
private:
    FloatBuffer(const FloatBuffer &);
    FloatBuffer &operator=(const FloatBuffer&);
};

class SampleBuffer : public Buffer {
public:
    double *values;
    uint64_t *times;
    double getFloatAtOffset(int offset) const;
    double getFloatAtIndex(int idx) const;
    
    void append( double val, uint64_t time);
    void quickAppend( double val, uint64_t time);
    
    double rate() const; // returns dv/dt between the two sample positions
    
    SampleBuffer(int buf_size) : Buffer(buf_size) { values = new double[buf_size]; times = new uint64_t[buf_size]; }
    ~SampleBuffer() { delete[] values; delete[] times; }

private:
    SampleBuffer(const SampleBuffer &);
    SampleBuffer &operator=(const SampleBuffer&);
};

class ButterworthFilter {
	public:
		ButterworthFilter(int num_c, double *c_coeff, int num_d, double *d_coeff) 
			: num_c_coefficients(num_c), c_coefficients(c_coeff), signal_buf(num_c+1), 
			  num_d_coefficients(num_d), d_coefficients(d_coeff), filtered_buf(num_d+1) {
		}
		float filter(float value);
	protected:
		int num_c_coefficients;
		double *c_coefficients;
		FloatBuffer signal_buf;
		int num_d_coefficients;
		double *d_coefficients;
		FloatBuffer filtered_buf;
};

#endif

