#pragma once
#include <atomic>
#include <cstdint>

struct AccelSample
{
    // Signed BMA150 values in the native 10-bit +/-2 g range (256 LSB/g).
    int16_t x, y, z;
};

class SampleProvider
{
public:
    virtual ~SampleProvider() = default;
    virtual AccelSample GetSample() = 0;

    std::atomic<bool> is_enabled = false;
};
