#pragma once

#include <cstdint>
class PocketWalker;

namespace AndroidRuntimeBridge
{
void SetEmulator(PocketWalker* emulator);
void ClearEmulator(PocketWalker* emulator);
bool SetAccelerometerEnabled(bool enabled);
bool PushAccelerometerSample(float x, float y, float z);
bool PushMotionPulse();
uint64_t ExternalAccelerometerReadCount();
uint64_t MotionBatchesAccepted();
uint64_t MotionStepAwards();
uint16_t MotionLastAcceptedStepsQ9();
}
