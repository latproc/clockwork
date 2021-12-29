#include <boost/thread.hpp>
#include <math.h>
//#include <boost/thread/mutex.hpp>
#include "filtering.h"

#if 0
class scoped_lock {
public:
    scoped_lock(boost::recursive_mutex &mut) : mutex(mut) { mutex.lock(); }
    ~scoped_lock() { mutex.unlock(); }
    boost::recursive_mutex &mutex;
};
#endif

Buffer::Buffer(int buf_size) : BUFSIZE(buf_size) {
    front = -1;
    back = -1;
}

unsigned int Buffer::length() const {
    if (front == -1) {
        return 0;
    }
    return (front - back + BUFSIZE) % BUFSIZE + 1;
}

void Buffer::reset() {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    front = back = -1;
    total_ = 0.0;
}

double Buffer::difference(int idx_a, int idx_b) const {
    double a = getFloatAtOffset(idx_a);
    double b = getFloatAtOffset(idx_b);
    a = a - b;
    if (fabs(a) >= 0.0001) {
        return a;
    }
    return 0.0f;
}

double Buffer::distance(int idx_a, int idx_b) const {
    double a = difference(idx_a, idx_b);
    a = fabs(a);
    if (a < 0.0001) {
        a = 0.0f;
    }
    return a;
}

double Buffer::average(int n) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    double res = 0.0f;
    if (front == -1) {
        return 0.0; // empty buffer
    }
    if (n == 0) {
        return 0.0;
    }
    int len = length();
    if (len <= 0) {
        return 0.0;
    }
    if (len < n) {
        n = len;
    }
    //  return total_ / (double)n;
#if 1
    int i = (front + BUFSIZE - n + 1) % BUFSIZE;
    while (i != front) {
#ifdef TESTING
        std::cout << i << ":" << getFloatAtIndex(i) << " ";
#endif
        res += getFloatAtIndex(i);
        i = (i + 1) % BUFSIZE;
    }
#ifdef TESTING
    std::cout << i << ":" << getFloatAtIndex(i) << "\n";
#endif
    res += getFloatAtIndex(i);
    return res / (double)n;
#endif
}

double Buffer::stddev(int n) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    double avg = average(n);
    double res = 0.0;
    if (front == -1) {
        return 0.0; // empty buffer
    }
    if (n == 0) {
        return 0.0;
    }
    int len = length();
    if (len <= 1) {
        return 0.0;
    }
    if (len < n) {
        n = len;
    }
    int i = (front + BUFSIZE - n + 1) % BUFSIZE;
    do {
        double val = getFloatAtIndex(i) - avg;
        res += val * val;
        i = (i + 1) % BUFSIZE;
    } while (i != front);
    return sqrt(res / (double)(n - 1));
}

void LongBuffer::append(long val) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    front = (front + 1) % BUFSIZE;
    if (front == back) {
        total_ -= buf[front];
    }
    buf[front] = val;
    total_ += val;
    if (front == back || back == -1) {
        back = (back + 1) % BUFSIZE;
    }
}
long LongBuffer::get(unsigned int n) const { return buf[(front + BUFSIZE - n) % BUFSIZE]; }
void LongBuffer::set(unsigned int n, long value) { buf[(front + BUFSIZE - n) % BUFSIZE] = value; }

double LongBuffer::getFloatAtOffset(int offset) const { return get(offset); }

double LongBuffer::getFloatAtIndex(int idx) const { return buf[idx]; }

double FloatBuffer::getFloatAtOffset(int offset) const { return get(offset); }

double FloatBuffer::getFloatAtIndex(int idx) const { return buf[idx]; }

int findMovement(FloatBuffer *buf, double amount, int max_len) {
    /* reading the buffer without entering data is a non-recoverable error */
    int l = buf->length();
    if (l == 0) {
        assert(0);
        abort();
    }
    int n = 0;
    int idx = buf->front;
    double current = buf->buf[idx];

    while (idx != buf->back && n < max_len) {
        idx--;
        if (idx == -1) {
            idx = buf->BUFSIZE - 1;
        }
        ++n;
        if (fabs(current - buf->buf[idx]) >= amount) {
            return n;
        }
    }
    return n;
}

double FloatBuffer::inner_product(double *coefficients, unsigned int num_coeff) const {
    float sum = 0;
    unsigned int l = length();
    for (unsigned int i = 0; i < num_coeff; i++) {
        float x = (i < length()) ? get(i) : 0;
        sum += x * coefficients[i];
    }
    return sum;
}

void FloatBuffer::append(double val) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    front = (front + 1) % BUFSIZE;
    if (front == back) {
        total_ -= buf[front];
    }
    buf[front] = val;
    total_ += val;
    if (front == back || back == -1) {
        back = (back + 1) % BUFSIZE;
    }
}

double FloatBuffer::get(unsigned int n) const { return buf[(front + BUFSIZE - n) % BUFSIZE]; }

void FloatBuffer::set(unsigned int n, double value) {
    buf[(front + BUFSIZE - n) % BUFSIZE] = value;
}

double FloatBuffer::slopeFromLeastSquaresFit(const LongBuffer &time_buf) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;
    double sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f;
    double sumXsquared = 0.0f, sumYsquared = 0.0f;
    int n = length() - 1;
    if (n <= 0) {
        return 0;
    }
    double t0 = time_buf.get(n);
    for (int i = n; i > 0; i--) {
        double y = (get(i) - get(n));
        double x = time_buf.get(i) - t0;
        sumX += x;
        sumY += y;
        sumXsquared += x * x;
        sumYsquared += y * y;
        sumXY += x * y;
    }
    double denom = (double)n * sumXsquared - sumX * sumX;
    if (fabs(denom) < 0.00001f) {
        return 0.0f;
    }
    double m = ((double)n * sumXY - sumX * sumY) / denom;
    //double c = (sumXsquared * sumY - sumXY * sumX) / denom;
    return m;
}

double FloatBuffer::movingAverage(unsigned int n) const {
    if (n == 0) {
        return 0.0;
    }
    if (length() < n) {
        n = length();
    }
    double sum = 0;
    for (unsigned int i = 0; i < n; i++) {
        sum += get(i);
    }
    return sum / n;
}

float ButterworthFilter::filter(float x) {
    signal_buf.append(x);
    double s2 = signal_buf.inner_product(c_coefficients,
                                         num_c_coefficients); // convolved input
    double s1 =
        filtered_buf.inner_product(d_coefficients + 1, num_d_coefficients - 1); // convolved output

    float filtered = (float)(s2 - s1);
    filtered_buf.append(filtered);
    return filtered;
}

double SampleBuffer::getFloatAtOffset(int offset) const {
    return values[(front + BUFSIZE - offset) % BUFSIZE];
}

double SampleBuffer::getFloatAtIndex(int idx) const { return values[idx]; }
void SampleBuffer::quickAppend(double val, uint64_t time) {
    front = (front + 1) % BUFSIZE;
    if (front == back) {
        total_ -= values[front];
    }
    if (front == back || back == -1) {
        back = (back + 1) % BUFSIZE;
    }
    total_ += val;
    values[front] = val;
    times[front] = time;
}

void SampleBuffer::append(double val, uint64_t time) {
    boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);
    ;

    /*  if we are not running on a real time system, we may have missed samples
        the following generates the missing samples based on past recording rates.
    */
    if (front != -1 && time == times[front]) {
        return;
    }
    unsigned int n = length();

    if (n > (unsigned int)BUFSIZE / 2) {
        double mean_change = ((values[front] - values[back])) / n;
        double period = ((double)(times[front] - times[back])) / n;
        int missing = ((double)(time - times[front])) / period;
        if (missing >= 3) {
            //DBG_MSG << "synthesizing missing data: " << missing << "\n";
            uint64_t last_time = times[front];
            reset();
            if (missing > BUFSIZE / 2) {
                missing = BUFSIZE / 2;
            }
            for (int i = missing; i > 0; --i) {
                quickAppend(val - i * mean_change, time - i * (time - last_time) / missing);
            }
        }
    }
    front = (front + 1) % BUFSIZE;
    if (front == back) {
        total_ -= values[front];
    }
    if (front == back || back == -1) {
        back = (back + 1) % BUFSIZE;
    }
    total_ += val;
    values[front] = val;
    times[front] = time;
}

double SampleBuffer::rate() const // returns dv/dt between the two sample positions
{
    if (front == back || back == -1) {
        return 0.0f;
    }
    double v1 = values[back], v2 = values[front];
    double t1 = times[back], t2 = times[front];
    double ds = v2 - v1;
    double dt = t2 - t1;
    return ds / dt;
}

#ifdef TESTING
#include <inttypes.h>
#include <iostream>

int main(int argc, char *argv[]) {
    FloatBuffer fb(4);
    long now = 0;
    for (int i = 0; i < 10; ++i) {
        std::cout << "t: " << now << "\n";
        fb.append(now);
        int x = i % 2 - 1;
        std::cout << "x: " << x << "\n";
        now += 10 * i % 2 - 1;
    }
    std::cout << fb.average(4) << " " << (long)fb.average(4) << "\n";
    return 0;
}
#endif
