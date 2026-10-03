#include "android_runtime_bridge.h"

#include <mutex>

#include "core/pokewalker/pocketwalker.h"

namespace
{
std::mutex emulator_mutex;
PocketWalker* active_emulator = nullptr;
}

void AndroidRuntimeBridge::SetEmulator(PocketWalker* emulator)
{
    std::scoped_lock lock(emulator_mutex);
    active_emulator = emulator;
}

void AndroidRuntimeBridge::ClearEmulator(PocketWalker* emulator)
{
    std::scoped_lock lock(emulator_mutex);
    if (active_emulator == emulator)
    {
        active_emulator->UseExternalAccelerometer(false);
        active_emulator->UseSyntheticSteps(false);
        active_emulator = nullptr;
    }
}

bool AndroidRuntimeBridge::SetAccelerometerEnabled(const bool enabled)
{
    std::scoped_lock lock(emulator_mutex);
    if (!active_emulator)
        return !enabled;
    if (enabled && active_emulator->IsRtcCatchUpActive())
        return false;

    active_emulator->UseExternalAccelerometer(enabled);
    return true;
}

bool AndroidRuntimeBridge::PushAccelerometerSample(const float x, const float y, const float z)
{
    std::scoped_lock lock(emulator_mutex);
    if (!active_emulator || active_emulator->IsRtcCatchUpActive())
        return false;

    active_emulator->UseExternalAccelerometer(true);
    active_emulator->SetExternalAcceleration(x, y, z);
    return true;
}

bool AndroidRuntimeBridge::PushMotionPulse()
{
    std::scoped_lock lock(emulator_mutex);
    if (!active_emulator || active_emulator->IsRtcCatchUpActive())
        return false;

    active_emulator->UseExternalAccelerometer(true);
    active_emulator->PulseExternalMotion();
    return true;
}

uint64_t AndroidRuntimeBridge::ExternalAccelerometerReadCount()
{
    std::scoped_lock lock(emulator_mutex);
    return active_emulator ? active_emulator->ExternalAccelerometerReadCount() : 0;
}

uint64_t AndroidRuntimeBridge::MotionBatchesAccepted()
{
    std::scoped_lock lock(emulator_mutex);
    return active_emulator ? active_emulator->MotionBatchesAccepted() : 0;
}

uint64_t AndroidRuntimeBridge::MotionStepAwards()
{
    std::scoped_lock lock(emulator_mutex);
    return active_emulator ? active_emulator->MotionStepAwards() : 0;
}

uint16_t AndroidRuntimeBridge::MotionLastAcceptedStepsQ9()
{
    std::scoped_lock lock(emulator_mutex);
    return active_emulator ? active_emulator->MotionLastAcceptedStepsQ9() : 0;
}
