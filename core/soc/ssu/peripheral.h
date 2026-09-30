#pragma once
#include <cstdint>
#include "core/utils/event_handler.h"

struct PinEvent
{
    uint8_t pin;
    bool value;
};

class Peripheral
{
public:
    virtual ~Peripheral() = default;

    virtual void Receive(uint8_t data) = 0;
    virtual uint8_t Transmit() = 0;
    virtual void Cycle(uint32_t cycles) {}
    virtual void Reset() {}
    virtual bool ReceiveRequiresTransfer() const { return false; }

    EventHandler<PinEvent> OnInputPin;
    EventHandler<PinEvent> OnOutputPin;
};
