
#include <gtest/gtest.h>
#include <lib_clockwork_interpreter/includes.hpp>
// #include "gtest/gtest.h"
// #include <value.h>
// #include <buffering.h>
//
// #include "library_globals.c"
//
// #include <buffering.h>


	class BufferTest : public ::testing::Test {
		protected:

		void SetUp() override {

		}
	};

	TEST_F(BufferTest, createBuffer) {
		// struct CircularBuffer *createBuffer(int size);
		struct CircularBuffer *buf = createBuffer(1);
		EXPECT_NE(nullptr, buf) << "creates a buffer when the size if valid";
		destroyBuffer(buf);
		buf = createBuffer(0);
		EXPECT_EQ(nullptr, buf) << "does nothing when the size is invalid";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, destroyBuffer) {
		// void destroyBuffer(struct CircularBuffer *buf);
	}

	TEST_F(BufferTest, size) {
		// int size(struct CircularBuffer *buf);
		struct CircularBuffer *buf = createBuffer(5);
		int buffer_size = size(buf);
		EXPECT_EQ(5, buffer_size) << "returns the capacity of the buffer";
		addSample(buf, 1.0, 1.0);
		EXPECT_EQ(5, buffer_size) << "returns the capacity of the buffer";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, bufferLength) {
		// int length(struct CircularBuffer *buf);
		struct CircularBuffer *buf = createBuffer(5);
		EXPECT_EQ(0, bufferLength(buf)) << "returns the number of elements in the buffer";
		addSample(buf, 1.0, 1.0);
		EXPECT_EQ(1, bufferLength(buf)) << "returns the number of elements after one is added";
		for(size_t i=0; i<10; ++i) addSample(buf, i, i);
		EXPECT_EQ(5, bufferLength(buf)) << "returns the capacity of the buffer after lots are added";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, bufferAverage) {
		// double bufferAverage(struct CircularBuffer *buf, int n);
		struct CircularBuffer *buf = createBuffer(3);
		EXPECT_EQ(0, bufferAverage(buf,3)) << "returns zero when there are no samples";
		addSample(buf, 1, 10.0);
		addSample(buf, 1, 1.0);
		addSample(buf, 1, 2.0);
		addSample(buf, 1, 3.0);
		EXPECT_EQ(2.0, bufferAverage(buf,3)) << "returns the average of the buffer samples";
		EXPECT_EQ(2.0, bufferAverage(buf,5)) << "returns the average when the sample count greater than the buffer size";
		EXPECT_EQ(0, bufferAverage(buf,-1)) << "returns 0 when the sample count is invalid";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, bufferSum) {
		// double bufferSum(struct CircularBuffer *buf, int n);
	}

	TEST_F(BufferTest, getBufferValue) {
		// double getBufferValue(struct CircularBuffer *buf, int n);
	}

	TEST_F(BufferTest, addSample) {
		// void addSample(struct CircularBuffer *buf, long time, double val);
	}

	TEST_F(BufferTest, findMovement) {
		/* seek back along the buffer to find the number of samples
			before a total movement of amount occurred, ignoring direction */
		// int findMovement(struct CircularBuffer *buf, double amount, int max_len);
	}

	TEST_F(BufferTest, getBufferValueAt) {
		// double getBufferValueAt(struct CircularBuffer *buf, unsigned long t); /* return an estimate of the value at time t */
		struct CircularBuffer *buf = createBuffer(3);
		//EXPECT_EQ(0, getBufferValueAt(buf,1)) << "calls abort when there are no values"
		addSample(buf, 2, 1.0);
		addSample(buf, 4, 2.0);
		addSample(buf, 6, 3.0);
		EXPECT_EQ(1.0, getBufferValueAt(buf,1)) << "returns the buffer value when known";
		EXPECT_EQ(1.0, getBufferValueAt(buf,0)) << "returns the earliest entry when the time is earlier than the earliest sample";
		EXPECT_EQ(1.5, getBufferValueAt(buf,3)) << "returns an estimate when the time is earlier than the earliest sample";
		EXPECT_EQ(3.0, getBufferValueAt(buf,8)) << "returns the latest entry when the time is later than the latest sample";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, rate) {
		/* calculate the rate of change by a direct method */
		// double rate(struct CircularBuffer *buf, int n);
		struct CircularBuffer *buf = createBuffer(3);
		addSample(buf, 2, 1.0);
		addSample(buf, 4, 2.0);
		addSample(buf, 6, 3.0);
		EXPECT_EQ(0.5, rate(buf,3)) << "returns and estimate of rate";
		destroyBuffer(buf);
	}

	TEST_F(BufferTest, slope) {
		/* calculate the rate of change using a least squares fit (slow) */
		// double slope(struct CircularBuffer *buf);
		struct CircularBuffer *buf = createBuffer(3);
		//EXPECT_EQ(0, getBufferValueAt(buf,1)) << "calls abort when there are no values"
		addSample(buf, 2, 1.0);
		addSample(buf, 4, 2.0);
		addSample(buf, 6, 3.0);
		EXPECT_EQ(0.5, rate(buf,3)) << "returns and estimate of slope";
		destroyBuffer(buf);
	}


	int main(int argc, char* argv[])
	{
	    testing::InitGoogleTest(&argc, argv);
	    return RUN_ALL_TESTS();
	}
