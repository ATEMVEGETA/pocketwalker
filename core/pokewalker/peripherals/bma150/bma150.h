#pragma once
#include <array>
#include <atomic>
#include <istream>
#include <memory>
#include <ostream>

#include "core/memory/memory.h"
#include "core/soc/ssu/peripheral.h"
#include "sample_provider.h"

#define BMA150_PIN_INT 4

#define BMA150_ADDR_CHIP_ID    0x00
#define BMA150_ADDR_VERSION    0x01
#define BMA150_ADDR_ACC_X_LSB  0x02
#define BMA150_ADDR_ACC_X_MSB  0x03
#define BMA150_ADDR_ACC_Y_LSB  0x04
#define BMA150_ADDR_ACC_Y_MSB  0x05
#define BMA150_ADDR_ACC_Z_LSB  0x06
#define BMA150_ADDR_ACC_Z_MSB  0x07
#define BMA150_ADDR_CONTROL_1  0x0A
#define BMA150_ADDR_CONF_1     0x0B
#define BMA150_ADDR_RANGE_BW_REG 0x14
#define BMA150_ADDR_CONF_2     0x15
#define BMA150_ADDR_PROTECTED_1E 0x1E

static constexpr uint8_t BMA150_CHIP_ID = 0x02;
static constexpr uint8_t BMA150_NEW_DATA_BIT = 0x01;
static constexpr uint8_t BMA150_SLEEP_BIT = 0x01;
static constexpr uint8_t BMA150_DATA_READY_ENABLE = 0x20;
static constexpr uint8_t BMA150_PW_DATA_READY_ENABLE = 0x80;

static constexpr std::array<uint16_t, 8> BMA150_CLOCK_RATES = {
    50, 100, 190, 375, 750, 1500, 3000, 3000
};

enum class BMA150State
{
    IDLE,
    MEMORY
};

class BMA150 : public Peripheral
{
public:
    explicit BMA150();

    void Receive(uint8_t data) override;
    uint8_t Transmit() override;
    void Reset() override;
    void Cycle(uint32_t cycles) override;
    bool ReceiveRequiresTransfer() const override { return true; }
    void SaveEmulatorState(std::ostream& stream);
    bool LoadEmulatorState(std::istream& stream);
    uint8_t ReadRegister(uint8_t address)
    {
        return mem.Read8(address & 0x7F);
    }

    uint64_t ConversionCount() const
    {
        return conversion_count.load(std::memory_order_relaxed);
    }

    uint64_t InterruptCount() const
    {
        return interrupt_count.load(std::memory_order_relaxed);
    }

    uint64_t BurstReadCount() const
    {
        return burst_read_count.load(std::memory_order_relaxed);
    }

    uint64_t ControlWriteCount() const
    {
        return control_write_count.load(std::memory_order_relaxed);
    }

    void SetSampleProvider(const std::shared_ptr<SampleProvider>& provider)
    {
        sample_provider = provider;
    }

    h8300h_ptr<uint8_t> control1 = nullptr;

private:
    void WriteRegister(uint8_t address, uint8_t value);
    void LatchAccelerationSample();
    void SetAxisRegisters(uint8_t lsb_address, int16_t value);
    void ClearNewDataFlags();
    void PulseDataReadyInterrupt();

    Memory<0x80> mem = {};

    BMA150State state = BMA150State::IDLE;
    bool is_reading = false;
    uint8_t register_index = 0;
    uint32_t cycle_count = 0;
    uint8_t offset = 0;
    uint8_t transfer_response = 0xFF;
    bool transfer_response_pending = false;
    bool wake_stabilizing = false;
    std::atomic<uint64_t> conversion_count = 0;
    std::atomic<uint64_t> interrupt_count = 0;
    std::atomic<uint64_t> burst_read_count = 0;
    std::atomic<uint64_t> control_write_count = 0;

    std::shared_ptr<SampleProvider> sample_provider = nullptr;
};
