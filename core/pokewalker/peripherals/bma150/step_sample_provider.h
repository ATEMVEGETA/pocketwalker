#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include "sample_provider.h"

static constexpr std::array<int16_t, 8> STEP_SINE_LUT = {
    0, 32, 44, 32, 0, -32, -44, -32
};

static constexpr auto EXTERNAL_MOTION_HOLD = std::chrono::milliseconds(850);

class StepSampleProvider : public SampleProvider
{
public:
    AccelSample GetSample() override
    {
        if (external_mode.load(std::memory_order_relaxed))
        {
            external_read_count.fetch_add(1, std::memory_order_relaxed);
            if (!ExternalMotionActive())
            {
                external_output_x = 0;
                external_output_y = 0;
                external_output_z = 0;
                synthetic_phase = 0;
                return {0, 0, 0};
            }

            const uint8_t phase = synthetic_phase.load(std::memory_order_relaxed);
            const int16_t x = STEP_SINE_LUT[phase];
            const uint8_t next_phase = static_cast<uint8_t>((phase + 1) & 7);
            synthetic_phase = next_phase;

            const AccelSample sample{x, static_cast<int16_t>(x / 2), 0};
            external_output_x = sample.x;
            external_output_y = sample.y;
            external_output_z = sample.z;
            return sample;
        }

        if (!is_enabled.load(std::memory_order_relaxed))
            return {0, 0, 0};

        const uint8_t phase = synthetic_phase.fetch_add(1, std::memory_order_relaxed) & 7;
        const int16_t sample = STEP_SINE_LUT[phase];
        return {sample, static_cast<int16_t>(sample / 2), 0};
    }

    void SetContinuous(bool enabled)
    {
        external_mode = false;
        is_enabled = enabled;
        if (!enabled)
            synthetic_phase = 0;
    }

    void SetExternalMode(bool enabled)
    {
        const bool previous = external_mode.exchange(enabled, std::memory_order_acq_rel);
        is_enabled = enabled;
        if (previous == enabled)
            return;

        external_read_count = 0;
        external_maximum_delta = 0;
        external_magnitude_squared = 0;
        external_output_x = 0;
        external_output_y = 0;
        external_output_z = 0;
        external_motion_deadline_ns = 0;
        external_motion_pulse_count = 0;
        synthetic_phase = 0;
    }

    void SetExternalSample(int16_t x, int16_t y, int16_t z)
    {
        const int previous_x = external_x.exchange(x);
        const int previous_y = external_y.exchange(y);
        const int previous_z = external_z.exchange(z);

        const auto abs_delta = [](int current, int previous) {
            const int delta = current - previous;
            return delta < 0 ? -delta : delta;
        };
        const int maximum_delta = std::max(
            abs_delta(x, previous_x),
            std::max(abs_delta(y, previous_y), abs_delta(z, previous_z)));
        const int64_t magnitude_squared =
            static_cast<int64_t>(x) * static_cast<int64_t>(x) +
            static_cast<int64_t>(y) * static_cast<int64_t>(y) +
            static_cast<int64_t>(z) * static_cast<int64_t>(z);

        external_maximum_delta = maximum_delta;
        external_magnitude_squared = magnitude_squared;
    }

    bool PulseExternalMotion()
    {
        external_motion_pulse_count.fetch_add(1, std::memory_order_relaxed);
        const int64_t deadline = SteadyNowNanoseconds() +
            std::chrono::duration_cast<std::chrono::nanoseconds>(EXTERNAL_MOTION_HOLD).count();
        external_motion_deadline_ns = deadline;
        return true;
    }

    uint64_t ExternalReadCount() const
    {
        return external_read_count.load(std::memory_order_relaxed);
    }

    AccelSample ExternalSample() const
    {
        return {
            static_cast<int16_t>(external_x.load(std::memory_order_relaxed)),
            static_cast<int16_t>(external_y.load(std::memory_order_relaxed)),
            static_cast<int16_t>(external_z.load(std::memory_order_relaxed))
        };
    }

    AccelSample ExternalOutputSample() const
    {
        return {
            static_cast<int16_t>(external_output_x.load(std::memory_order_relaxed)),
            static_cast<int16_t>(external_output_y.load(std::memory_order_relaxed)),
            static_cast<int16_t>(external_output_z.load(std::memory_order_relaxed))
        };
    }

    int ExternalMaximumDelta() const
    {
        return external_maximum_delta.load(std::memory_order_relaxed);
    }

    int64_t ExternalMagnitudeSquared() const
    {
        return external_magnitude_squared.load(std::memory_order_relaxed);
    }

    bool ExternalMotionActive() const
    {
        return external_motion_deadline_ns.load(std::memory_order_relaxed) >
               SteadyNowNanoseconds();
    }

    uint64_t ExternalMotionPulseCount() const
    {
        return external_motion_pulse_count.load(std::memory_order_relaxed);
    }

    uint32_t ExternalMotionRemainingMilliseconds() const
    {
        const int64_t remaining = external_motion_deadline_ns.load(std::memory_order_relaxed) -
                                  SteadyNowNanoseconds();
        return remaining > 0 ? static_cast<uint32_t>(remaining / 1'000'000) : 0;
    }

private:
    static int64_t SteadyNowNanoseconds()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<uint8_t> synthetic_phase = 0;
    std::atomic<bool> external_mode = false;
    std::atomic<int> external_x = 0;
    std::atomic<int> external_y = 0;
    std::atomic<int> external_z = 256;
    std::atomic<uint64_t> external_read_count = 0;
    std::atomic<int> external_maximum_delta = 0;
    std::atomic<int64_t> external_magnitude_squared = 0;
    std::atomic<int> external_output_x = 0;
    std::atomic<int> external_output_y = 0;
    std::atomic<int> external_output_z = 0;
    std::atomic<int64_t> external_motion_deadline_ns = 0;
    std::atomic<uint64_t> external_motion_pulse_count = 0;
};
