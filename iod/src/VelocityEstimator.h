#ifndef __VELOCITYESTIMATOR_H__
#define __VELOCITYESTIMATOR_H__

#include <cmath>
#include <cstddef>
#include <cstdint>

/*
 * Deterministic time-window rate estimator used by VELOCITY machine instances.
 *
 * This class deliberately owns no Clockwork state. It consumes raw source
 * values with their source timestamps and returns one coherent result whose
 * IOTIME is the timestamp of the newest sample contributing to VALUE.
 */
class VelocityEstimator {
  public:
    enum State {
        SETTLING,
        VALID,
        STALE,
        REJECTED
    };

    struct Result {
        double value;
        uint64_t iotime;
        double raw_value;
        uint64_t raw_iotime;
        uint64_t window_start;
        uint64_t window_us;
        double delta_value;
        uint64_t delta_time_us;
        uint64_t age_ms;
        size_t sample_count;
        int valid;
        int rejected;
        State state;

        Result()
            : value(0.0), iotime(0), raw_value(0), raw_iotime(0), window_start(0), window_us(0),
              delta_value(0), delta_time_us(0), age_ms(0), sample_count(0), valid(0), rejected(0),
              state(SETTLING) {}
    };

    VelocityEstimator(size_t capacity = 128, uint64_t window_us = 100000,
                      size_t min_samples = 4, double scale = 1.0, uint64_t stale_us = 250000,
                      double max_abs_value = 0.0)
        : capacity_(capacity < 2 ? 2 : capacity), start_(0), count_(0), window_us_(window_us),
          min_samples_(min_samples < 2 ? 2 : min_samples), scale_(scale), stale_us_(stale_us),
          max_abs_value_(max_abs_value), last_result_() {
        samples_ = new Sample[capacity_];
        configure(window_us, min_samples, scale, stale_us, max_abs_value);
    }

    ~VelocityEstimator() { delete[] samples_; }

    VelocityEstimator(const VelocityEstimator &) = delete;
    VelocityEstimator &operator=(const VelocityEstimator &) = delete;

    void configure(uint64_t window_us, size_t min_samples, double scale, uint64_t stale_us,
                   double max_abs_value) {
        window_us_ = window_us == 0 ? 100000 : window_us;
        min_samples_ = min_samples < 2 ? 2 : min_samples;
        scale_ = scale;
        stale_us_ = stale_us;
        max_abs_value_ = max_abs_value;
        last_result_ = evaluate(last_result_.raw_iotime);
    }

    void reset() {
        start_ = 0;
        count_ = 0;
        last_result_ = Result();
    }

    bool addSample(uint64_t time_us, double value, uint64_t now_us) {
        if (count_ > 0) {
            const Sample &newest = at(count_ - 1);
            if (time_us <= newest.time_us) {
                last_result_ = evaluate(now_us);
                last_result_.rejected = 1;
                last_result_.valid = 0;
                last_result_.state = REJECTED;
                return false;
            }
        }

        if (count_ == capacity_) {
            start_ = (start_ + 1) % capacity_;
            --count_;
        }
        samples_[(start_ + count_) % capacity_] = Sample{time_us, value};
        ++count_;

        const uint64_t cutoff = time_us > window_us_ ? time_us - window_us_ : 0;
        while (count_ > 1 && at(0).time_us < cutoff) {
            start_ = (start_ + 1) % capacity_;
            --count_;
        }
        last_result_ = evaluate(now_us);
        return true;
    }

    Result evaluate(uint64_t now_us) const {
        Result result;
        if (count_ == 0) {
            result.state = STALE;
            return result;
        }

        const Sample &newest = at(count_ - 1);
        const Sample &oldest = at(0);
        result.raw_value = newest.value;
        result.raw_iotime = newest.time_us;
        result.iotime = newest.time_us;
        result.window_start = oldest.time_us;
        result.window_us = newest.time_us - oldest.time_us;
        result.delta_value = newest.value - oldest.value;
        result.delta_time_us = result.window_us;
        result.sample_count = count_;
        result.age_ms = now_us >= newest.time_us ? (now_us - newest.time_us) / 1000 : 0;

        if (stale_us_ > 0 && result.age_ms * 1000 > stale_us_) {
            result.state = STALE;
            return result;
        }
        if (count_ < min_samples_ || result.window_us == 0) {
            result.state = SETTLING;
            return result;
        }

        const double x0 = static_cast<double>(oldest.time_us);
        double sum_x = 0.0;
        double sum_y = 0.0;
        double sum_xx = 0.0;
        double sum_xy = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            const Sample &s = at(i);
            const double x = static_cast<double>(s.time_us) - x0;
            const double y = s.value;
            sum_x += x;
            sum_y += y;
            sum_xx += x * x;
            sum_xy += x * y;
        }
        const double n = static_cast<double>(count_);
        const double denominator = n * sum_xx - sum_x * sum_x;
        if (std::fabs(denominator) < 0.000001) {
            result.state = REJECTED;
            result.rejected = 1;
            return result;
        }

        const double counts_per_us = (n * sum_xy - sum_x * sum_y) / denominator;
        result.value = counts_per_us * 1000000.0 * scale_;
        if (max_abs_value_ > 0.0 && std::fabs(result.value) > max_abs_value_) {
            result.value = 0.0;
            result.state = REJECTED;
            result.rejected = 1;
            return result;
        }
        result.valid = 1;
        result.state = VALID;
        return result;
    }

    const Result &lastResult() const { return last_result_; }

  private:
    struct Sample {
        uint64_t time_us;
        double value;
    };

    const Sample &at(size_t index) const { return samples_[(start_ + index) % capacity_]; }

    Sample *samples_;
    size_t capacity_;
    size_t start_;
    size_t count_;
    uint64_t window_us_;
    size_t min_samples_;
    double scale_;
    uint64_t stale_us_;
    double max_abs_value_;
    Result last_result_;
};

#endif
