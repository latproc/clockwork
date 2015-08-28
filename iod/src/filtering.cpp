#include <math.h>
#include <boost/thread.hpp>
//#include <boost/thread/mutex.hpp>
#include "filtering.h"

#if 0
class scoped_lock {
public:
	scoped_lock( boost::recursive_mutex &mut) : mutex(mut) { mutex.lock(); }
	~scoped_lock() { mutex.unlock(); }
	boost::recursive_mutex &mutex;
};
#endif

Buffer::Buffer(int buf_size): BUFSIZE(buf_size) {
    front = -1;
    back = -1;
}

int Buffer::length()
{
    if (front == -1) return 0;
    return (front - back + BUFSIZE) % BUFSIZE + 1;
}

void Buffer::reset() {
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;
    front = back = -1;
	total_ = 0.0;
}

float Buffer::difference(int idx_a, int idx_b) const
{
  float a = getFloatAtOffset(idx_a);
  float b = getFloatAtOffset(idx_b);
  a = a - b;
  if (fabs(a) >= 0.0001) return a;
  return 0.0f;
}

float Buffer::distance(int idx_a, int idx_b) const
{
  float a = difference(idx_a, idx_b);
  a = fabs(a);
  if (a<0.0001) a = 0.0f;
  return a;
}

float Buffer::average(int n)
{
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;
  float res = 0.0f;
  if (front == -1) return 0.0f; // empty buffer
  if (n == 0) return 0.0f;
  int len = length();
  if (len <= 0) return 0.0f;
  if (len < n) n = len;
  return total_ / n;
#if 0
  int i = (front + BUFSIZE - n + 1) % BUFSIZE;
  while (i != front)
  {
#ifdef TESTING
    std::cout << i << ":" <<getFloatAtIndex(i) << " ";
#endif
    res += getFloatAtIndex(i);
    i = (i+1) % BUFSIZE;
  }
#ifdef TESTING
  std::cout << i << ":" <<getFloatAtIndex(i) << "\n";
#endif
  res += getFloatAtIndex(i);
  return res / n;
#endif
}

void LongBuffer::append(long val)
{
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;
    front = (front + 1) % BUFSIZE;
	if (front == back) total_ -= buf[front];
    buf[front] = val;
	total_ += val;
    if (front == back || back == -1)
        back = (back + 1) % BUFSIZE;
}
long LongBuffer::get(unsigned int n) const
{
    return buf[ (front + BUFSIZE - n) % BUFSIZE];
}
void LongBuffer::set(unsigned int n, long value)
{
    buf[ (front + BUFSIZE - n) % BUFSIZE] = value;
}

float LongBuffer::getFloatAtOffset(int offset) const
{
    return get(offset);
}

float LongBuffer::getFloatAtIndex(int idx) const
{
    return buf[idx];
}

float FloatBuffer::getFloatAtOffset(int offset) const
{
    return get(offset);
}

float FloatBuffer::getFloatAtIndex(int idx) const
{
    return buf[idx];
}

void FloatBuffer::append( float val)
{
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;
    front = (front + 1) % BUFSIZE;
	if (front == back) total_ -= buf[front];
    buf[front] = val;
	total_ += val;
    if (front == back || back == -1)
      back = (back + 1) % BUFSIZE;
}

float FloatBuffer::get(unsigned int n) const
{
    return buf[ (front + BUFSIZE - n) % BUFSIZE];
}

void FloatBuffer::set(unsigned int n, float value)
{
    buf[ (front + BUFSIZE - n) % BUFSIZE] = value;
}

float FloatBuffer::slopeFromLeastSquaresFit(const LongBuffer &time_buf)
{
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;
  float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f;
  float sumXsquared = 0.0f, sumYsquared = 0.0f;
  int n = length()-1;
	if (n<=0) return 0;
  float t0 = time_buf.get(n);
  for (int i = n; i>0; i--)
  {
    float y = (get(i) - get(n));
    float x = time_buf.get(i) - t0;
    sumX += x;
    sumY += y;
    sumXsquared += x*x;
    sumYsquared += y*y;
    sumXY += x*y;
  }
  float denom = (float)n*sumXsquared - sumX*sumX;
	if (fabs(denom) < 0.00001f ) return 0.0f;
  float m = ((float)n * sumXY - sumX*sumY) / denom;
  //float c = (sumXsquared * sumY - sumXY * sumX) / denom;
  return m;
}


float SampleBuffer::getFloatAtOffset(int offset) const {
    return values[ (front + BUFSIZE - offset) % BUFSIZE];
}

float SampleBuffer::getFloatAtIndex(int idx) const {
    return values[ idx ];
}
void SampleBuffer::quickAppend( float val, uint64_t time) {
	front = (front + 1) % BUFSIZE;
	if (front == back) total_ -= values[front];
	if (front == back || back == -1) back = (back + 1) % BUFSIZE;
	total_ += val;
	values[front] = val;
	times[front] = time;
}

void SampleBuffer::append( float val, uint64_t time) {
	boost::recursive_mutex::scoped_lock scoped_lock(q_mutex);;

    /* if we are not running on a real time system, we may have missed samples
       the following generates the missing samples based on past recording rates.
     */
	if (front != -1 && time == times[front]) return;
	unsigned int n = length();
  
  if (n > (unsigned int)BUFSIZE/2) {
		double mean_change = ((values[front] - values[back])) / n;
		double period = ((double)(times[front] - times[back])) / n;
		int missing = ((double)(time - times[front]) )/ period;
		if (missing >= 3){
			//DBG_MSG << "synthesizing missing data: " << missing << "\n";
			uint64_t last_time = times[front];
			reset();
			if (missing > BUFSIZE/2) missing = BUFSIZE/2;
			for (int i=missing; i>0; --i)
			quickAppend(val-i*mean_change, time - i * (time-last_time)/missing);
		}
	}
	front = (front + 1) % BUFSIZE;
	if (front == back) total_ -= values[front];
	if (front == back || back == -1) back = (back + 1) % BUFSIZE;
	total_ += val;
	values[front] = val;
	times[front] = time;
}

float SampleBuffer::rate() const // returns dv/dt between the two sample positions
{
    if (front == back || back == -1) return 0.0f;
    double v1 = values[back], v2 = values[front];
    double t1 = times[back], t2 = times[front];
    double ds = v2-v1;
    double dt = t2-t1;
    return ds/dt;
}



