#pragma once
#include <atomic>
#include <cstdint>

struct AccelSample
{
    int8_t x, y, z;
};

class SampleProvider
{
public:
    virtual ~SampleProvider() = default;
    virtual AccelSample GetSample() = 0;

    std::atomic<bool> is_enabled = false;
};
