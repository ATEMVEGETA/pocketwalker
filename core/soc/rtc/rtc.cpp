#include "rtc.h"

#include <chrono>
#include <cmath>

#include "core/soc/defines.h"

static uint8_t BCD(const uint8_t number)
{
    const uint8_t tens = std::floor(number / 10);
    const uint8_t ones = number % 10;

    return tens * 16 + ones;
}

RTC::RTC(const std::shared_ptr<Interrupts>& interrupts)
{
    this->interrupts = interrupts;
}

void RTC::RegisterIOHandlers(const std::shared_ptr<IO>& io)
{
    IO_HANDLER_READ_UNION(RTC_ADDR_RTCCR1, RTCCR1);
    IO_HANDLER_WRITE_UNION(RTC_ADDR_RTCCR1, RTCCR1);

    IO_HANDLER_READ_VALUE(RTC_ADDR_RSECDR, RSECDR);
    IO_HANDLER_WRITE_VALUE(RTC_ADDR_RSECDR, RSECDR);

    IO_HANDLER_READ_VALUE(RTC_ADDR_RMINDR, RMINDR);
    IO_HANDLER_WRITE_VALUE(RTC_ADDR_RMINDR, RMINDR);

    IO_HANDLER_READ_VALUE(RTC_ADDR_RHRDR, RHRDR);
    IO_HANDLER_WRITE_VALUE(RTC_ADDR_RHRDR, RHRDR);

    IO_HANDLER_READ_VALUE(RTC_ADDR_RWKDR, RWKDR);
    IO_HANDLER_WRITE_VALUE(RTC_ADDR_RWKDR, RWKDR);
}

void RTC::Cycle(uint8_t cycles)
{
    if (!RTCCR1.RUN)
        return;

    rtc_cycles += cycles;
    if (rtc_cycles >= PHI_CLK / 4)
    {
        rtc_cycles -= PHI_CLK / 4;

        const time_t now = std::time(nullptr);
        std::tm current_time = {};
#ifdef _WIN32
        localtime_s(&current_time, &now);
#else
        localtime_r(&now, &current_time);
#endif

        RSECDR = BCD(current_time.tm_sec);
        RMINDR = BCD(current_time.tm_min);
        RHRDR = BCD(RTCCR1.HR24 ? current_time.tm_hour : current_time.tm_hour % 12);
        RWKDR = BCD(current_time.tm_wday);

        if (!initialized)
        {
            interrupts->RTCFLG.SEIFG025 = true;
            interrupts->RTCFLG.SEIFG05 = true;
            interrupts->RTCFLG.SEIFG1 = true;
            interrupts->RTCFLG.MNIFG = true;
            interrupts->RTCFLG.HRIFG = true;
            interrupts->RTCFLG.DYIFG = true;
            interrupts->RTCFLG.WKIFG = true;

            last_time = current_time;
            initialized = true;
        }

        quarters++;

        interrupts->RTCFLG.SEIFG025 = true;

        if (quarters % 2 == 0)
            interrupts->RTCFLG.SEIFG05 = true;

        if (quarters % 4 == 0)
        {
            quarters = 0;

            if (current_time.tm_sec != last_time.tm_sec)
                interrupts->RTCFLG.SEIFG1 = true;

            if (current_time.tm_min != last_time.tm_min)
                interrupts->RTCFLG.MNIFG = true;

            if (current_time.tm_hour != last_time.tm_hour)
                interrupts->RTCFLG.HRIFG = true;

            if (current_time.tm_mday != last_time.tm_mday) [[unlikely]]
                interrupts->RTCFLG.DYIFG = true;

            if (current_time.tm_wday != last_time.tm_wday) [[unlikely]]
                interrupts->RTCFLG.WKIFG = true;

            last_time = current_time;
        }
    }
}
