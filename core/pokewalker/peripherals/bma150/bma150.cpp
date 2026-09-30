#include "bma150.h"

#include <algorithm>

#include "core/soc/defines.h"

BMA150::BMA150()
{
    mem.Write8(BMA150_ADDR_CHIP_ID, BMA150_CHIP_ID);
    control1 = mem.Ptr8(BMA150_ADDR_CONTROL_1);
    *control1 = BMA150_SLEEP_BIT;
}

void BMA150::Receive(uint8_t data)
{
    switch (state)
    {
    case BMA150State::IDLE:
        is_reading = data & 0b10000000;
        register_index = data & 0b01111111;
        offset = 0;
        transfer_response = 0xFF;
        transfer_response_pending = true;
        state = BMA150State::MEMORY;
        break;
    case BMA150State::MEMORY:
        if (is_reading)
        {
            const uint8_t address = static_cast<uint8_t>((register_index + offset++) & 0x7F);
            transfer_response = mem.Read8(address);
            transfer_response_pending = true;
            if (address == BMA150_ADDR_ACC_Z_MSB)
            {
                burst_read_count.fetch_add(1, std::memory_order_relaxed);
                ClearNewDataFlags();
            }
        }
        else
        {
            WriteRegister(register_index++, data);
            transfer_response = 0xFF;
            transfer_response_pending = true;
        }
        break;
    }
}

uint8_t BMA150::Transmit()
{
    // SSU may poll its receive side again before another byte is written.
    // A physical SPI device does not clock or advance on such a poll, so keep
    // returning the last response until Receive() observes a real transfer.
    if (transfer_response_pending)
        transfer_response_pending = false;
    return transfer_response;
}

void BMA150::Reset()
{
    state = BMA150State::IDLE;
    offset = 0;
    transfer_response = 0xFF;
    transfer_response_pending = false;
}

void BMA150::Cycle(uint32_t cycles)
{
    if (*control1 & BMA150_SLEEP_BIT)
    {
        cycle_count = 0;
        wake_stabilizing = false;
        return;
    }

    constexpr uint32_t WAKE_UP_CYCLES = PHI_CLK / 1000;

    const uint16_t clock_rate = BMA150_CLOCK_RATES[
        mem.Read8(BMA150_ADDR_RANGE_BW_REG) & 0b111];
    const uint32_t conversion_cycles = std::max<uint32_t>(1, PHI_CLK / clock_rate);
    const uint32_t target_cycles = wake_stabilizing ? WAKE_UP_CYCLES : conversion_cycles;

    cycle_count += cycles;
    if (cycle_count < target_cycles)
        return;

    cycle_count -= target_cycles;
    wake_stabilizing = false;
    LatchAccelerationSample();
    // The Pokewalker firmware sleeps while waiting for the BMA150 conversion.
    // Completing a conversion must wake it even when loading an older state
    // whose undocumented configuration registers were not preserved.
    PulseDataReadyInterrupt();
}

void BMA150::WriteRegister(const uint8_t address, const uint8_t value)
{
    control_write_count.fetch_add(1, std::memory_order_relaxed);
    const uint8_t old_value = mem.Read8(address);
    mem.Write8(address, value);

    if (address != BMA150_ADDR_CONTROL_1)
        return;

    const bool was_sleeping = (old_value & BMA150_SLEEP_BIT) != 0;
    const bool is_sleeping = (value & BMA150_SLEEP_BIT) != 0;
    if (is_sleeping)
    {
        cycle_count = 0;
        wake_stabilizing = false;
    }
    else if (was_sleeping)
    {
        cycle_count = 0;
        wake_stabilizing = true;
    }
}

void BMA150::LatchAccelerationSample()
{
    const AccelSample sample = sample_provider
        ? sample_provider->GetSample()
        : AccelSample{0, 0, 256};

    SetAxisRegisters(BMA150_ADDR_ACC_X_LSB, sample.x);
    SetAxisRegisters(BMA150_ADDR_ACC_Y_LSB, sample.y);
    SetAxisRegisters(BMA150_ADDR_ACC_Z_LSB, sample.z);
    conversion_count.fetch_add(1, std::memory_order_relaxed);
}

void BMA150::SetAxisRegisters(const uint8_t lsb_address, const int16_t value)
{
    const int16_t clamped = std::clamp<int16_t>(value, -512, 511);
    const uint16_t raw = static_cast<uint16_t>(clamped) & 0x03FF;
    mem.Write8(lsb_address,
               static_cast<uint8_t>(((raw & 0x03) << 6) | BMA150_NEW_DATA_BIT));
    mem.Write8(static_cast<uint8_t>(lsb_address + 1),
               static_cast<uint8_t>((raw >> 2) & 0xFF));
}

void BMA150::ClearNewDataFlags()
{
    for (const uint8_t address : {
             BMA150_ADDR_ACC_X_LSB,
             BMA150_ADDR_ACC_Y_LSB,
             BMA150_ADDR_ACC_Z_LSB})
    {
        mem.Write8(address,
                   static_cast<uint8_t>(mem.Read8(address) & ~BMA150_NEW_DATA_BIT));
    }
}

void BMA150::PulseDataReadyInterrupt()
{
    interrupt_count.fetch_add(1, std::memory_order_relaxed);
    OnOutputPin({BMA150_PIN_INT, true});
    OnOutputPin({BMA150_PIN_INT, false});
}

void BMA150::SaveEmulatorState(std::ostream& stream)
{
    for (uint16_t i = 0; i < 0x80; i++)
    {
        const uint8_t value = mem.Read8(i);
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    const uint8_t state_value = static_cast<uint8_t>(state);
    stream.write(reinterpret_cast<const char*>(&state_value), sizeof(state_value));
    stream.write(reinterpret_cast<const char*>(&is_reading), sizeof(is_reading));
    stream.write(reinterpret_cast<const char*>(&register_index), sizeof(register_index));
    stream.write(reinterpret_cast<const char*>(&cycle_count), sizeof(cycle_count));
    stream.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
}

bool BMA150::LoadEmulatorState(std::istream& stream)
{
    for (uint16_t i = 0; i < 0x80; i++)
    {
        uint8_t value = 0;
        stream.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (!stream)
            return false;
        mem.Write8(i, value);
    }

    uint8_t state_value = 0;
    stream.read(reinterpret_cast<char*>(&state_value), sizeof(state_value));
    stream.read(reinterpret_cast<char*>(&is_reading), sizeof(is_reading));
    stream.read(reinterpret_cast<char*>(&register_index), sizeof(register_index));
    stream.read(reinterpret_cast<char*>(&cycle_count), sizeof(cycle_count));
    stream.read(reinterpret_cast<char*>(&offset), sizeof(offset));
    if (!stream)
        return false;

    if (state_value > static_cast<uint8_t>(BMA150State::MEMORY))
        state_value = static_cast<uint8_t>(BMA150State::IDLE);

    state = static_cast<BMA150State>(state_value);
    register_index &= 0x7F;
    transfer_response = 0xFF;
    transfer_response_pending = false;
    wake_stabilizing = (*control1 & BMA150_SLEEP_BIT) == 0;
    conversion_count = 0;
    interrupt_count = 0;
    burst_read_count = 0;
    control_write_count = 0;

    // States produced by the older emulator stored the BMA150's immutable
    // identity as zero. Migrate only the emulated sensor registers; firmware
    // RAM and all game counters remain untouched.
    if (mem.Read8(BMA150_ADDR_CHIP_ID) != BMA150_CHIP_ID)
    {
        mem.Write8(BMA150_ADDR_CHIP_ID, BMA150_CHIP_ID);
        mem.Write8(BMA150_ADDR_CONF_2, 0x80);
        mem.Write8(BMA150_ADDR_RANGE_BW_REG,
                   static_cast<uint8_t>((mem.Read8(BMA150_ADDR_RANGE_BW_REG) & 0xE0) | 0x06));
        mem.Write8(BMA150_ADDR_PROTECTED_1E,
                   static_cast<uint8_t>(mem.Read8(BMA150_ADDR_PROTECTED_1E) |
                                        BMA150_PW_DATA_READY_ENABLE));
    }
    return true;
}
