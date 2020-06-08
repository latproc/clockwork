#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include <rate.h>

using ::testing::Return;
using ::testing::ReturnPointee;

class MockTimeSource : public cw::TimeSource {
  public:
	MOCK_METHOD(uint64_t, now, (), (const, override));
	MOCK_METHOD(Units, units, (), (const, override));
	MOCK_METHOD(uint64_t, to_microsecs, (), (const override));
};

TEST( RateLimiter, ReturnsTrueWhenNotEnabled) {
  MockTimeSource time_source;
  RateLimiter rl(100, RateLimiter::boaNone, time_source); 
  rl.enable(false);
  EXPECT_EQ(rl.ready(), true);
}

TEST(RateLimiter, IsInitiallyEnabled) {
  RateLimiter rl;
  EXPECT_EQ(rl.enabled(), true);
}

TEST(RateLimiter, NotReadyUntilDelayHasExpired) {
  uint64_t dummy_time = 1;
  MockTimeSource time_source;
  EXPECT_CALL(time_source, now()).WillRepeatedly(ReturnPointee(&dummy_time));
  EXPECT_CALL(time_source, to_microsecs()).WillRepeatedly(Return(1000));
  dummy_time = 1100;
  RateLimiter rl(100, RateLimiter::boaNone, time_source); 
  EXPECT_EQ(rl.ready(), true);
  dummy_time = 1199;
  EXPECT_EQ(rl.ready(), false);
  dummy_time = 1200;
  EXPECT_EQ(rl.ready(), true);
}

