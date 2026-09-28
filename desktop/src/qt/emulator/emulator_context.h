#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <optional>
#include <QObject>
#include <QThread>
#include "core/pokewalker/pocketwalker.h"
#include "desktop/src/qt/audio/qt_audio_system.h"
#include "desktop/src/qt/network/qt_network_system.h"
#include "desktop/src/qt/application_args.h"

class EmulatorContext : public QObject
{
    Q_OBJECT

public:
    explicit EmulatorContext(const std::string& rom_path, const std::string& save_path,
                             const ApplicationArguments& args = {}, QObject* parent = nullptr,
                             bool defer_network_start = false);
    explicit EmulatorContext(const std::string& rom_path,
                             const ApplicationArguments& args = {}, QObject* parent = nullptr);
    ~EmulatorContext() override;

    PocketWalker& emulator() { return *emu; }
    const std::string& savePath() const { return save_path; }
    const std::string& romPath() const { return rom_path; }
    const std::string& rtcLastActiveDate() const { return rtc_last_active_date; }
    bool checkpointSave();
    void startNetwork();
    void setAudioEnabled(bool enabled) { audio_enabled.store(enabled, std::memory_order_relaxed); }

signals:
    void networkStatusChanged(const QString& status);
    void networkHostDiscovered(const QString& host);

private:
    void loadSave();
    void writeSave();

    std::string rom_path;
    std::string save_path;
    std::string rtc_last_active_date;
    ApplicationArguments network_args;
    std::optional<PocketWalker> emu;
    std::unique_ptr<QtAudioSystem> audio;
    std::unique_ptr<QtNetworkSystem> network;
    std::unique_ptr<QThread> network_thread;
    std::unique_ptr<std::thread> emulator_thread;
    std::atomic<bool> audio_enabled = true;
    bool checkpoint_in_progress = false;
};
