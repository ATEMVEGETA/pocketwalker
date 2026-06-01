#include "qt_audio_system.h"
#include <QMediaDevices>
#include <QThread>

#include "desktop/src/qt/settings/app_settings.h"

QtAudioSystem::QtAudioSystem(QObject* parent) : QObject(parent)
{
    QAudioFormat fmt;
    fmt.setSampleRate(AUDIO_SAMPLE_RATE);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    sink->setBufferSize(AUDIO_SAMPLE_RATE * sizeof(int16_t) / 10);
    device = sink->start();

    buffer.reserve(AUDIO_FLUSH_EVERY * 2);
}

QtAudioSystem::~QtAudioSystem()
{
    Flush();
    sink->disconnect();
    sink->stop();
    device = nullptr;
    QThread::msleep(50);
    delete sink;
    sink = nullptr;
}

void QtAudioSystem::PushSample(BuzzerInformation info)
{
    const bool is_active = info.frequency >= AUDIO_MIN_FREQUENCY;

    if (is_active && !playing)
    {
        current_freq = info.frequency;
        playing = true;
        fade_counter = 0;
    }
    else if (!is_active && playing)
    {
        playing = false;
        fade_counter = AUDIO_FADE_SAMPLES;
    }
    else if (is_active && info.frequency != current_freq)
    {
        current_freq = info.frequency;
    }

    float raw = 0.0f;
    if (current_freq >= AUDIO_MIN_FREQUENCY && (playing || fade_counter > 0))
    {
        const float nyquist = AUDIO_SAMPLE_RATE / 2.0f;
        const int max_harmonic = static_cast<int>(std::floor(nyquist / current_freq));
        float sample = 0.0f;

        for (int h = 1; h <= max_harmonic; h += 2)
            sample += std::sin(2.0f * M_PI * static_cast<float>(h) * phase) / static_cast<float>(h);

        const float amplitude = info.is_full_volume
                                    ? static_cast<float>(AUDIO_BASE_AMPLITUDE)
                                    : static_cast<float>(AUDIO_BASE_AMPLITUDE) / 2.0f;

        raw = sample * static_cast<float>(4.0f / M_PI) * amplitude;

        if (playing && fade_counter < AUDIO_FADE_SAMPLES)
        {
            const float t = static_cast<float>(fade_counter) / static_cast<float>(AUDIO_FADE_SAMPLES);
            raw *= t * t;
            fade_counter++;
        }
        else if (!playing && fade_counter > 0)
        {
            const float t = static_cast<float>(fade_counter) / static_cast<float>(AUDIO_FADE_SAMPLES);
            raw *= t * t;
            fade_counter--;
        }

        raw = std::clamp(raw, -32768.0f, 32767.0f) * AppSettings::instance.audio.volume;
    }

    phase += current_freq / static_cast<float>(AUDIO_SAMPLE_RATE);
    phase -= std::floor(phase);

    buffer.push_back(static_cast<int16_t>(raw));
    if (static_cast<int>(buffer.size()) >= AUDIO_FLUSH_EVERY)
        Flush();
}

void QtAudioSystem::Flush()
{
    if (buffer.empty() || !device) return;
    device->write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(int16_t));
    buffer.clear();
}