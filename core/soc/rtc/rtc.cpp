#include "rtc.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "core/soc/defines.h"

using Clock = std::chrono::steady_clock;

namespace
{
constexpr auto CATCH_UP_MIN_MIDNIGHT_HOLD = std::chrono::milliseconds(10);
constexpr auto CATCH_UP_MAX_MIDNIGHT_HOLD = std::chrono::milliseconds(250);
constexpr size_t MAX_FIRMWARE_CATCH_UP_MIDNIGHTS = 8;
}

static uint8_t BCD(const uint8_t number)
{
    const uint8_t tens = std::floor(number / 10);
    const uint8_t ones = number % 10;

    return tens * 16 + ones;
}

static uint8_t FromBCD(const uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

static std::tm LocalTime(const time_t value)
{
    std::tm result = {};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

static bool TryParseLocalTime(const std::string& text, time_t& value)
{
    std::tm parsed = {};
    std::istringstream stream(text);
    stream >> std::get_time(&parsed, "%Y-%m-%d %H:%M:%S");

    if (stream.fail())
        return false;

    parsed.tm_isdst = -1;
    value = std::mktime(&parsed);
    return value != static_cast<time_t>(-1);
}

static time_t CurrentHostTime()
{
    if (const char* env_time = std::getenv("POCKETWALKER_CLOCK"))
    {
        time_t parsed = 0;
        if (TryParseLocalTime(env_time, parsed))
            return parsed;
    }

    const auto clock_path = std::filesystem::current_path() / "pocketwalker_clock.txt";
    std::ifstream clock_file(clock_path);
    if (clock_file)
    {
        std::string text;
        std::getline(clock_file, text);

        time_t parsed = 0;
        if (TryParseLocalTime(text, parsed))
            return parsed;
    }

    return std::time(nullptr);
}

static bool TryReadClockFile(const std::filesystem::path& path, time_t& value)
{
    std::ifstream clock_file(path);
    if (!clock_file)
        return false;

    std::string text;
    std::getline(clock_file, text);
    return TryParseLocalTime(text, value);
}

static bool TryReadSyncClock(time_t& value)
{
    if (const char* env_time = std::getenv("POCKETWALKER_SYNC_CLOCK"))
    {
        if (TryParseLocalTime(env_time, value))
            return true;
    }

    const auto sync_path = std::filesystem::current_path() / "pocketwalker_sync_clock.txt";
    if (!TryReadClockFile(sync_path, value))
        return false;

    std::error_code error;
    std::filesystem::remove(sync_path, error);
    return true;
}

static time_t NextLocalMidnightAfter(const time_t value)
{
    std::tm time = LocalTime(value);
    time.tm_sec = 0;
    time.tm_min = 0;
    time.tm_hour = 0;
    time.tm_mday += 1;
    time.tm_isdst = -1;
    return std::mktime(&time);
}

RTC::RTC(const std::shared_ptr<Interrupts>& interrupts)
{
    this->interrupts = interrupts;
    this->virtual_time = CurrentHostTime();
}

void RTC::RegisterIOHandlers(const std::shared_ptr<IO>& io)
{
    IO_HANDLER_READ_UNION(RTC_ADDR_RTCCR1, RTCCR1);
    io->RegisterWriteHandler(RTC_ADDR_RTCCR1, [this](const uint8_t value) { WriteRTCCR1(value); });

    IO_HANDLER_READ_VALUE(RTC_ADDR_RSECDR, RSECDR);
    io->RegisterWriteHandler(RTC_ADDR_RSECDR, [this](const uint8_t value) { WriteSeconds(value); });

    IO_HANDLER_READ_VALUE(RTC_ADDR_RMINDR, RMINDR);
    io->RegisterWriteHandler(RTC_ADDR_RMINDR, [this](const uint8_t value) { WriteMinutes(value); });

    IO_HANDLER_READ_VALUE(RTC_ADDR_RHRDR, RHRDR);
    io->RegisterWriteHandler(RTC_ADDR_RHRDR, [this](const uint8_t value) { WriteHours(value); });

    IO_HANDLER_READ_VALUE(RTC_ADDR_RWKDR, RWKDR);
    io->RegisterWriteHandler(RTC_ADDR_RWKDR, [this](const uint8_t value) { WriteWeekday(value); });
}

void RTC::LoadState(const std::string& path)
{
    const auto now_wall = Clock::now();
    ignore_rtc_writes_until = now_wall + std::chrono::seconds(3);
    wall_clock_initialized = false;
    catch_up_allowed_to_run = false;
    catch_up_midnights.clear();
    catch_up_midnight_index = 0;
    catch_up_target_time = 0;
    catch_up_current_midnight = 0;
    catch_up_overflow_days = 0;
    has_pending_sync_time = false;
    pending_sync_time = 0;
    catch_up_hold_until = {};
    catch_up_force_next_after = {};

    time_t sync_time = 0;
    if (TryReadSyncClock(sync_time))
    {
        has_pending_sync_time = true;
        pending_sync_time = sync_time;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        virtual_time = CurrentHostTime();
        SetRegistersFromVirtualTime();
        last_time = LocalTime(virtual_time);
        return;
    }

    char magic[8] = {};
    int64_t saved_virtual_time = 0;
    int64_t saved_host_time = 0;
    f.read(magic, sizeof(magic));
    f.read(reinterpret_cast<char*>(&saved_virtual_time), sizeof(saved_virtual_time));
    f.read(reinterpret_cast<char*>(&saved_host_time), sizeof(saved_host_time));

    if (!f || std::string(magic, sizeof(magic)) != "PWRTC001")
    {
        virtual_time = CurrentHostTime();
    }
    else
    {
        const time_t now = CurrentHostTime();
        int64_t elapsed = static_cast<int64_t>(now) - saved_host_time;
        if (elapsed < 0)
            elapsed = 0;
        const time_t target_time = static_cast<time_t>(saved_virtual_time + elapsed);

        if (elapsed > 0)
        {
            StartCatchUp(static_cast<time_t>(saved_virtual_time), target_time);
        }
        else
        {
            virtual_time = target_time;
        }
    }

    SetRegistersFromVirtualTime();
    if (!initialized)
        last_time = LocalTime(virtual_time);
}

void RTC::ApplyPendingSyncClock()
{
    if (!has_pending_sync_time)
        return;

    virtual_time = pending_sync_time;
    pending_sync_time = 0;
    has_pending_sync_time = false;
    catch_up_allowed_to_run = false;
    catch_up_midnights.clear();
    catch_up_midnight_index = 0;
    catch_up_target_time = 0;
    catch_up_current_midnight = 0;
    catch_up_overflow_days = 0;
    catch_up_hold_until = {};
    catch_up_force_next_after = {};
    wall_clock_initialized = false;
    quarters = 0;
    interrupts->RTCFLG.VALUE = 0;

    SetRegistersFromVirtualTime();
    last_time = LocalTime(virtual_time);
    initialized = true;
    interrupts->RTCFLG.SEIFG025 = true;
    interrupts->RTCFLG.SEIFG05 = true;
    interrupts->RTCFLG.SEIFG1 = true;
    interrupts->RTCFLG.MNIFG = true;
    interrupts->RTCFLG.HRIFG = true;
}

bool RTC::IsCatchUpActive() const
{
    return catch_up_target_time != 0;
}

size_t RTC::CatchUpMidnightsCompleted() const
{
    return catch_up_midnight_index;
}

size_t RTC::CatchUpMidnightsTotal() const
{
    return catch_up_midnights.size();
}

uint32_t RTC::ConsumeCatchUpOverflowDays()
{
    const uint32_t result = catch_up_overflow_days;
    catch_up_overflow_days = 0;
    return result;
}

void RTC::AllowCatchUpToRun(bool value)
{
    if (catch_up_allowed_to_run == value)
        return;

    catch_up_allowed_to_run = value;
}

void RTC::StartCatchUp(const time_t saved_time, const time_t target_time)
{
    catch_up_midnights.clear();
    catch_up_midnight_index = 0;
    catch_up_target_time = 0;
    catch_up_current_midnight = 0;
    catch_up_overflow_days = 0;
    catch_up_hold_until = {};
    catch_up_force_next_after = {};

    for (time_t midnight = NextLocalMidnightAfter(saved_time);
         midnight > saved_time && midnight <= target_time;
         midnight = NextLocalMidnightAfter(midnight))
    {
        catch_up_midnights.push_back(midnight);
    }

    if (catch_up_midnights.empty())
    {
        virtual_time = target_time;
        last_time = LocalTime(saved_time);
        initialized = true;
        return;
    }

    if (catch_up_midnights.size() > MAX_FIRMWARE_CATCH_UP_MIDNIGHTS)
    {
        catch_up_overflow_days = static_cast<uint32_t>(catch_up_midnights.size() - MAX_FIRMWARE_CATCH_UP_MIDNIGHTS);
        catch_up_midnights.erase(catch_up_midnights.begin(), catch_up_midnights.end() - MAX_FIRMWARE_CATCH_UP_MIDNIGHTS);
    }

    virtual_time = catch_up_midnights.front() - 1;
    catch_up_target_time = target_time;
    last_time = LocalTime(virtual_time);
    initialized = true;

}

void RTC::CycleCatchUp()
{
    const auto now = Clock::now();

    if (catch_up_current_midnight != 0)
    {
        if (now < catch_up_hold_until)
            return;

        if ((interrupts->RTCFLG.DYIFG || interrupts->RTCFLG.WKIFG) && now < catch_up_force_next_after)
            return;

        catch_up_current_midnight = 0;
        catch_up_hold_until = {};
        catch_up_force_next_after = {};
    }

    if (catch_up_current_midnight == 0)
    {
        if (catch_up_midnight_index < catch_up_midnights.size())
        {
            catch_up_current_midnight = catch_up_midnights[catch_up_midnight_index++];
            virtual_time = catch_up_current_midnight - 1;
            last_time = LocalTime(virtual_time);
            quarters = 3;
            TickQuarter();
            catch_up_hold_until = now + CATCH_UP_MIN_MIDNIGHT_HOLD;
            catch_up_force_next_after = now + CATCH_UP_MAX_MIDNIGHT_HOLD;
            return;
        }

        virtual_time = catch_up_target_time;
        SetRegistersFromVirtualTime();
        last_time = LocalTime(virtual_time);
        interrupts->RTCFLG.SEIFG025 = true;
        interrupts->RTCFLG.SEIFG05 = true;
        interrupts->RTCFLG.SEIFG1 = true;
        interrupts->RTCFLG.MNIFG = true;
        interrupts->RTCFLG.HRIFG = true;
        interrupts->RTCFLG.DYIFG = true;
        interrupts->RTCFLG.WKIFG = true;
        catch_up_target_time = 0;
        catch_up_midnights.clear();
        catch_up_midnight_index = 0;
        catch_up_current_midnight = 0;
        catch_up_hold_until = {};
        catch_up_force_next_after = {};
        wall_clock_initialized = false;
        return;
    }
}

void RTC::SaveState(const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return;

    const char magic[8] = {'P', 'W', 'R', 'T', 'C', '0', '0', '1'};
    const int64_t saved_virtual_time = static_cast<int64_t>(virtual_time);
    const int64_t saved_host_time = static_cast<int64_t>(CurrentHostTime());

    f.write(magic, sizeof(magic));
    f.write(reinterpret_cast<const char*>(&saved_virtual_time), sizeof(saved_virtual_time));
    f.write(reinterpret_cast<const char*>(&saved_host_time), sizeof(saved_host_time));
}

void RTC::Cycle(uint8_t cycles)
{
    (void)cycles;

    if (IsCatchUpActive())
    {
        if (catch_up_allowed_to_run)
            CycleCatchUp();
        return;
    }

    if (!RTCCR1.RUN)
    {
        wall_clock_initialized = false;
        return;
    }

    const auto now = Clock::now();
    if (!wall_clock_initialized)
    {
        last_wall_tick = now;
        wall_clock_initialized = true;
        return;
    }

    const auto tick = std::chrono::milliseconds(250);
    while (now - last_wall_tick >= tick)
    {
        last_wall_tick += tick;
        TickQuarter();
    }
}

void RTC::TickQuarter()
{
    quarters++;

    if (quarters % 4 == 0)
        virtual_time++;

    const std::tm current_time = LocalTime(virtual_time);
    SetRegistersFromVirtualTime();

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

void RTC::SetRegistersFromVirtualTime()
{
    const std::tm current_time = LocalTime(virtual_time);
    RSECDR = BCD(current_time.tm_sec);
    RMINDR = BCD(current_time.tm_min);
    if (RTCCR1.HR24)
    {
        RHRDR = BCD(current_time.tm_hour);
    }
    else
    {
        RTCCR1.PM = current_time.tm_hour >= 12;
        RHRDR = BCD(current_time.tm_hour % 12);
    }
    RWKDR = BCD(current_time.tm_wday);
}

void RTC::SyncVirtualTimeFromRegisters()
{
    std::tm current_time = LocalTime(virtual_time);
    current_time.tm_sec = std::min<uint8_t>(FromBCD(RSECDR), 59);
    current_time.tm_min = std::min<uint8_t>(FromBCD(RMINDR), 59);

    uint8_t hour = FromBCD(RHRDR);
    if (!RTCCR1.HR24)
    {
        hour %= 12;
        if (RTCCR1.PM)
            hour += 12;
    }
    current_time.tm_hour = std::min<uint8_t>(hour, 23);
    current_time.tm_isdst = -1;

    virtual_time = std::mktime(&current_time);
    last_time = LocalTime(virtual_time);
}

bool RTC::CanAcceptRtcWrites() const
{
    return Clock::now() >= ignore_rtc_writes_until;
}

void RTC::WriteRTCCR1(const uint8_t value)
{
    RTCCR1.VALUE = value;
    const bool accepted = CanAcceptRtcWrites();
    if (accepted)
        SyncVirtualTimeFromRegisters();
}

void RTC::WriteSeconds(const uint8_t value)
{
    RSECDR = value;
    const bool accepted = CanAcceptRtcWrites();
    if (accepted)
        SyncVirtualTimeFromRegisters();
}

void RTC::WriteMinutes(const uint8_t value)
{
    RMINDR = value;
    const bool accepted = CanAcceptRtcWrites();
    if (accepted)
        SyncVirtualTimeFromRegisters();
}

void RTC::WriteHours(const uint8_t value)
{
    RHRDR = value;
    const bool accepted = CanAcceptRtcWrites();
    if (accepted)
        SyncVirtualTimeFromRegisters();
}

void RTC::WriteWeekday(const uint8_t value)
{
    RWKDR = value;
    const bool accepted = CanAcceptRtcWrites();
    if (accepted)
        SyncVirtualTimeFromRegisters();
}
