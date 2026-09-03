#pragma once
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include "core/soc/interrupts/interrupts.h"

#define RTC_ADDR_RSECDR 0xF068
#define RTC_ADDR_RMINDR 0xF069
#define RTC_ADDR_RHRDR 0xF06A
#define RTC_ADDR_RWKDR 0xF06B
#define RTC_ADDR_RTCCR1 0xF06C

union RTCCR1_t
{
    uint8_t VALUE;

    struct
    {
        uint8_t : 3;
        uint8_t INT : 1;
        uint8_t RST : 1;
        uint8_t PM : 1;
        uint8_t HR24 : 1;
        uint8_t RUN : 1;
    };
};

class RTC
{
public:
    explicit RTC(const std::shared_ptr<Interrupts>& interrupts);

    void RegisterIOHandlers(const std::shared_ptr<IO>& io);

    void Cycle(uint8_t cycles);
    void LoadState(const std::string& path);
    void SaveState(const std::string& path);
    bool IsCatchUpActive() const;
    size_t CatchUpMidnightsCompleted() const;
    size_t CatchUpMidnightsTotal() const;
    uint32_t ConsumeCatchUpOverflowDays();
    void AllowCatchUpToRun(bool value);
    void ApplyPendingSyncClock();

    RTCCR1_t RTCCR1 = {};
    uint8_t RSECDR = 0;
    uint8_t RMINDR = 0;
    uint8_t RHRDR = 0;
    uint8_t RWKDR = 0;

private:
    std::shared_ptr<Interrupts> interrupts;

    uint32_t rtc_cycles = 0;
    uint8_t quarters = 0;
    bool initialized = false;
    bool wall_clock_initialized = false;
    bool catch_up_allowed_to_run = false;
    std::chrono::steady_clock::time_point last_wall_tick = {};
    std::chrono::steady_clock::time_point ignore_rtc_writes_until = {};
    std::chrono::steady_clock::time_point catch_up_hold_until = {};
    std::chrono::steady_clock::time_point catch_up_force_next_after = {};
    std::tm last_time = {};
    time_t virtual_time = 0;
    time_t pending_sync_time = 0;
    time_t catch_up_target_time = 0;
    time_t catch_up_current_midnight = 0;
    uint32_t catch_up_overflow_days = 0;
    bool has_pending_sync_time = false;
    std::vector<time_t> catch_up_midnights = {};
    size_t catch_up_midnight_index = 0;

    void SetRegistersFromVirtualTime();
    void SyncVirtualTimeFromRegisters();
    void StartCatchUp(time_t saved_time, time_t target_time);
    void CycleCatchUp();
    bool CanAcceptRtcWrites() const;
    void TickQuarter();
    void WriteRTCCR1(uint8_t value);
    void WriteSeconds(uint8_t value);
    void WriteMinutes(uint8_t value);
    void WriteHours(uint8_t value);
    void WriteWeekday(uint8_t value);
};
