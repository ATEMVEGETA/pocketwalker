#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include "sample_provider.h"
#include "core/memory/interface.h"

#define PW_ADDR_ACCEL_SAMPLE_COUNT 0xF7AE
#define PW_ADDR_IS_NOT_WALKING  0xF8EF

static constexpr std::array<int8_t, 8> STEP_SINE_LUT = {
    0, 18, 26, 18, 0, -18, -26, -18
};

static constexpr auto EXTERNAL_MOTION_HOLD = std::chrono::milliseconds(850);

class StepSampleProvider : public SampleProvider
{
public:
    explicit StepSampleProvider(const std::shared_ptr<MemoryInterface>& memory)
        : mem(memory) {}

    AccelSample GetSample() override
    {
        if (external_mode)
        {
            external_read_count.fetch_add(1, std::memory_order_relaxed);
            UpdateExternalStepProgress();
            if (!IsExternalMotionActive())
            {
                if (external_motion_active.exchange(false, std::memory_order_acq_rel))
                    mem->Write8(PW_ADDR_IS_NOT_WALKING, 1);
                external_output_x = 0;
                external_output_y = 0;
                external_output_z = 0;
                return {0, 0, 0};
            }

            external_motion_active = true;
            const uint8_t sample_count = mem->Read8(PW_ADDR_ACCEL_SAMPLE_COUNT);
            if (sample_count == 0)
                mem->Write8(PW_ADDR_IS_NOT_WALKING, 0);

            const int8_t s = STEP_SINE_LUT[sample_count & 7];
            const int8_t y = static_cast<int8_t>(s >> 1);
            external_output_x = s;
            external_output_y = y;
            external_output_z = 0;
            return {s, y, 0};
        }

        const uint8_t sample_count = mem->Read8(PW_ADDR_ACCEL_SAMPLE_COUNT);

        if (prime_next_sample.exchange(false) || sample_count == 0)
            mem->Write8(PW_ADDR_IS_NOT_WALKING, 0);

        const int8_t s = STEP_SINE_LUT[sample_count & 7];
        return {s, static_cast<int8_t>(s >> 1), 0};
    }

    void SetContinuous(bool enabled)
    {
        external_mode = false;
        if (enabled && !is_enabled.load())
        {
            // Prime the first emulated sample instead of waiting for the
            // accelerometer ring index to wrap to zero.
            prime_next_sample = true;
            is_enabled = true;
            return;
        }

        is_enabled = enabled;
        if (!enabled)
            prime_next_sample = false;
    }

    void SetExternalMode(bool enabled)
    {
        const bool previous = external_mode.exchange(enabled);
        is_enabled = enabled;
        if (previous == enabled)
            return;

        prime_next_sample = false;
        ResetExternalMotionDetector();
    }

    void SetExternalSample(int8_t x, int8_t y, int8_t z)
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
        const int magnitude_squared =
            static_cast<int>(x) * static_cast<int>(x) +
            static_cast<int>(y) * static_cast<int>(y) +
            static_cast<int>(z) * static_cast<int>(z);

        external_maximum_delta = maximum_delta;
        external_magnitude_squared = magnitude_squared;
    }

    bool PulseExternalMotion()
    {
        external_detected_steps.fetch_add(1, std::memory_order_relaxed);
        const int64_t now = SteadyNowNanoseconds();
        const int64_t previous_deadline = external_motion_deadline_ns.exchange(
            now + std::chrono::duration_cast<std::chrono::nanoseconds>(EXTERNAL_MOTION_HOLD).count(),
            std::memory_order_acq_rel);
        const bool was_active = previous_deadline > now;
        if (!was_active)
        {
            external_step_baseline_ready = false;
            external_motion_prime_count.fetch_add(1, std::memory_order_relaxed);
            external_motion_active = true;

            // Start a fresh firmware accelerometer window. The old provider
            // waited for this 64-sample ring to wrap, adding several seconds
            // of latency before the first visible step.
            mem->Write8(PW_ADDR_ACCEL_SAMPLE_COUNT, 0);
            mem->Write8(PW_ADDR_IS_NOT_WALKING, 0);
        }
        return !was_active;
    }

    uint64_t ExternalReadCount() const
    {
        return external_read_count.load(std::memory_order_relaxed);
    }

    AccelSample ExternalSample() const
    {
        return {
            static_cast<int8_t>(external_x.load()),
            static_cast<int8_t>(external_y.load()),
            static_cast<int8_t>(external_z.load())
        };
    }

    AccelSample ExternalOutputSample() const
    {
        return {
            static_cast<int8_t>(external_output_x.load()),
            static_cast<int8_t>(external_output_y.load()),
            static_cast<int8_t>(external_output_z.load())
        };
    }

    bool ExternalMotionActive() const
    {
        return IsExternalMotionActive();
    }

    uint64_t ExternalMotionPrimeCount() const
    {
        return external_motion_prime_count.load(std::memory_order_relaxed);
    }

    int ExternalMaximumDelta() const
    {
        return external_maximum_delta.load(std::memory_order_relaxed);
    }

    int ExternalMagnitudeSquared() const
    {
        return external_magnitude_squared.load(std::memory_order_relaxed);
    }

    uint64_t ExternalDetectedStepCount() const
    {
        return external_detected_steps.load(std::memory_order_relaxed);
    }

    uint64_t ExternalFirmwareStepCount() const
    {
        return external_firmware_steps.load(std::memory_order_relaxed);
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

    bool IsExternalMotionActive() const
    {
        return external_motion_deadline_ns.load(std::memory_order_relaxed) >
               SteadyNowNanoseconds();
    }

    void UpdateExternalStepProgress()
    {
        constexpr uint16_t SESSION_STEPS_ADDRESS = 0xF79C;
        const uint32_t current = mem->Read32(SESSION_STEPS_ADDRESS);
        if (!external_step_baseline_ready.exchange(true, std::memory_order_acq_rel))
        {
            external_last_session_steps = current;
            return;
        }

        const uint32_t previous = external_last_session_steps.exchange(current);
        if (current <= previous)
            return;

        const uint32_t produced = current - previous;
        external_firmware_steps.fetch_add(produced, std::memory_order_relaxed);
    }

    void ResetExternalMotionDetector()
    {
        external_detected_steps = 0;
        external_firmware_steps = 0;
        external_motion_deadline_ns = 0;
        external_motion_active = false;
        external_last_session_steps = 0;
        external_step_baseline_ready = false;
        external_maximum_delta = 0;
        external_magnitude_squared = 0;
        external_output_x = 0;
        external_output_y = 0;
        external_output_z = 0;
    }

    std::shared_ptr<MemoryInterface> mem;
    std::atomic<bool> prime_next_sample = false;
    std::atomic<bool> external_mode = false;
    std::atomic<int> external_x = 0;
    std::atomic<int> external_y = 0;
    std::atomic<int> external_z = 0;
    std::atomic<uint64_t> external_read_count = 0;
    std::atomic<uint64_t> external_motion_prime_count = 0;
    std::atomic<int> external_maximum_delta = 0;
    std::atomic<int> external_magnitude_squared = 0;
    std::atomic<int> external_output_x = 0;
    std::atomic<int> external_output_y = 0;
    std::atomic<int> external_output_z = 0;
    std::atomic<uint64_t> external_detected_steps = 0;
    std::atomic<uint64_t> external_firmware_steps = 0;
    std::atomic<int64_t> external_motion_deadline_ns = 0;
    std::atomic<bool> external_motion_active = false;
    std::atomic<uint32_t> external_last_session_steps = 0;
    std::atomic<bool> external_step_baseline_ready = false;
};
