#include "gtest/gtest.h"

#include <cmath>
#include <cstdint>

#include "VelocityEstimator.h"

namespace {

const double MM_PER_COUNT = 0.000192;
const uint64_t START_US = 1000000;
const uint64_t PERIOD_US = 20000;

int64_t position_at(double speed_mm_s, uint64_t elapsed_us) {
    const double counts = speed_mm_s * (elapsed_us / 1000000.0) / MM_PER_COUNT;
    return static_cast<int64_t>(std::llround(counts));
}

void feed_constant(VelocityEstimator &estimator, double speed_mm_s, int samples) {
    for (int i = 0; i < samples; ++i) {
        const uint64_t elapsed = static_cast<uint64_t>(i) * PERIOD_US;
        const uint64_t stamp = START_US + elapsed;
        estimator.addSample(stamp, position_at(speed_mm_s, elapsed), stamp);
    }
}

TEST(VelocityEstimator, constant_cruise_uses_a_fixed_time_window) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 1000.0);
    feed_constant(estimator, 500.0, 12);

    const VelocityEstimator::Result result = estimator.lastResult();
    EXPECT_EQ(VelocityEstimator::VALID, result.state);
    EXPECT_EQ(1, result.valid);
    EXPECT_NEAR(500.0, result.value, 2.0);
    EXPECT_EQ(START_US + 11 * PERIOD_US, result.iotime);
    EXPECT_EQ(result.iotime, result.raw_iotime);
    EXPECT_GE(result.sample_count, 10u);
    EXPECT_EQ(200000u, result.window_us);
}

TEST(VelocityEstimator, low_speed_does_not_use_a_fixed_movement_floor) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 1000.0);
    feed_constant(estimator, 10.0, 12);

    const VelocityEstimator::Result result = estimator.lastResult();
    EXPECT_EQ(VelocityEstimator::VALID, result.state);
    EXPECT_NEAR(10.0, result.value, 0.2);
}

TEST(VelocityEstimator, rest_becomes_zero_without_retaining_last_motion) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 1000.0);
    feed_constant(estimator, 500.0, 12);
    const int64_t stopped_position = position_at(500.0, 11 * PERIOD_US);

    for (int i = 12; i < 24; ++i) {
        const uint64_t stamp = START_US + static_cast<uint64_t>(i) * PERIOD_US;
        estimator.addSample(stamp, stopped_position, stamp);
    }

    const VelocityEstimator::Result result = estimator.lastResult();
    EXPECT_EQ(VelocityEstimator::VALID, result.state);
    EXPECT_NEAR(0.0, result.value, 0.01);
    EXPECT_EQ(0, result.delta_value);
}

TEST(VelocityEstimator, reports_stale_data_instead_of_reusing_old_velocity) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 1000.0);
    feed_constant(estimator, 500.0, 12);
    const uint64_t newest = START_US + 11 * PERIOD_US;

    const VelocityEstimator::Result result = estimator.evaluate(newest + 251000);
    EXPECT_EQ(VelocityEstimator::STALE, result.state);
    EXPECT_EQ(0, result.valid);
    EXPECT_EQ(0.0, result.value);
}

TEST(VelocityEstimator, timestamp_goes_backwards_is_rejected) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 1000.0);
    feed_constant(estimator, 500.0, 12);
    const uint64_t newest = START_US + 11 * PERIOD_US;

    EXPECT_FALSE(estimator.addSample(newest, 0, newest));
    const VelocityEstimator::Result result = estimator.lastResult();
    EXPECT_EQ(VelocityEstimator::REJECTED, result.state);
    EXPECT_EQ(1, result.rejected);
    EXPECT_EQ(0, result.valid);
}

TEST(VelocityEstimator, implausible_value_is_rejected) {
    VelocityEstimator estimator(128, 200000, 4, MM_PER_COUNT, 250000, 600.0);
    feed_constant(estimator, 700.0, 12);

    const VelocityEstimator::Result result = estimator.lastResult();
    EXPECT_EQ(VelocityEstimator::REJECTED, result.state);
    EXPECT_EQ(1, result.rejected);
    EXPECT_EQ(0, result.valid);
    EXPECT_EQ(0.0, result.value);
}

} // namespace
