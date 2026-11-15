// Hintergrund-Threads der Oberfläche: Vorschaubilder, Export und Tonausgabe.
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <QIODevice>
#include <QImage>
#include <QThread>

#include "loggx/engine/Engine.h"

// Rendert Vorschaubilder. Es zählt immer nur die neueste Anfrage: kommt
// die Oberfläche schneller voran als der Decoder, werden Bilder übersprungen
// statt aufgestaut.
class PreviewWorker : public QThread {
    Q_OBJECT
public:
    explicit PreviewWorker(QObject* parent = nullptr);
    ~PreviewWorker() override;

    // fast: während der Wiedergabe kleiner rechnen (flüssiger)
    void request(std::shared_ptr<const loggx::Project> project, loggx::TimeUs time, bool fast = false,
                 int maxHeight = 540);

signals:
    void frameReady(const QImage& image);
    void renderTime(double ms);

protected:
    void run() override;

private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::shared_ptr<const loggx::Project> m_project;
    loggx::TimeUs m_time = 0;
    bool m_fast = false;
    int m_maxHeight = 540;
    bool m_pending = false;
    bool m_quit = false;
};

class ExportWorker : public QThread {
    Q_OBJECT
public:
    enum Mode { Video, Audio, Stems };
    ExportWorker(loggx::Project project, loggx::ExportSettings settings, QObject* parent = nullptr,
                 Mode mode = Video, QString stemsExtension = {});
    void cancel() { m_cancel = true; }

signals:
    void progress(double value);
    void done(bool ok, bool cancelled, const QString& message);

protected:
    void run() override;

private:
    loggx::Project m_project;
    loggx::ExportSettings m_settings;
    Mode m_mode;
    QString m_stemsExt;
    std::atomic<bool> m_cancel{false};
};

// Quelle für die Live-Tonausgabe (Computer- und MIDI-Tastatur).
class LiveDevice : public QIODevice {
    Q_OBJECT
public:
    explicit LiveDevice(loggx::LiveSynth* synth, QObject* parent = nullptr);
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 16; }

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    loggx::LiveSynth* m_synth;
};

// Quelle für QAudioSink: zieht den Mix ab einer Startposition.
class MixerDevice : public QIODevice {
    Q_OBJECT
public:
    explicit MixerDevice(loggx::AudioMixer* mixer, QObject* parent = nullptr);
    void start(std::int64_t positionSamples);
    void stop();

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    loggx::AudioMixer* m_mixer;
    std::atomic<std::int64_t> m_position{0};
};
