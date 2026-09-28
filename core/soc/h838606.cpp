#include "h838606.h"

#include <array>
#include <cstdint>
#include <memory>
#include <thread>

#include "defines.h"
#include "core/utils/logger.h"

H838606::H838606(RomBuffer rom_buffer)
{
    auto rom = std::make_shared<ROM>(rom_buffer);
    auto ram = std::make_shared<RAM>();
    auto io = std::make_shared<IO>();

    memory = std::make_shared<MemoryBus>(rom, ram, io);
    cpu = std::make_shared<CPU>(memory);

    interrupts = std::make_shared<Interrupts>();
    interrupts->RegisterIOHandlers(io);

    ssu = std::make_shared<SSU>(memory, interrupts);
    ssu->RegisterIOHandlers(io);
    
    sci3 = std::make_shared<SCI3>();
    sci3->RegisterIOHandlers(io);

    timer_b1 = std::make_shared<TimerB1>(interrupts);
    timer_b1->RegisterIOHandlers(io);

    timer_w = std::make_shared<TimerW>(memory, interrupts);
    timer_w->RegisterIOHandlers(io);

    rtc = std::make_shared<RTC>(interrupts);
    rtc->RegisterIOHandlers(io);

    adc = std::make_shared<ADC>(memory);
    adc->RegisterIOHandlers(io);

    IO_HANDLER_READ_UNION(SOC_ADDR_CKSTPR1, CKSTPR1);
    IO_HANDLER_WRITE_UNION(SOC_ADDR_CKSTPR1, CKSTPR1);
    
    IO_HANDLER_READ_UNION(SOC_ADDR_CKSTPR2, CKSTPR2);
    IO_HANDLER_WRITE_UNION(SOC_ADDR_CKSTPR2, CKSTPR2);
}

uint8_t H838606::Cycle()
{
    const uint8_t cycles = cpu->Cycle();
    interrupts->Cycle(cpu);

    if (CKSTPR2.SSUCKSTP)
        ssu->Cycle(cycles);

    if (CKSTPR1.S3CKSTP)
        sci3->Cycle(cycles);

    if (CKSTPR1.TB1CKSTP)
        timer_b1->Cycle(cycles);

    if (CKSTPR2.TWCKSTP)
        timer_w->Cycle(cycles);

    if (CKSTPR1.RTCCKSTP)
        rtc->Cycle(cycles);

    if (CKSTPR1.ADCKSTP)
        adc->Cycle(cycles);

    return cycles;
}
