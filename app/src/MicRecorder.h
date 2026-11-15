// Mikrofon-Aufnahme: nimmt vom Audioeingang in eine WAV-Datei auf.
#pragma once

#include <QAudioDevice>
#include <QFile>
#include <QIODevice>
#include <QObject>

class QAudioSource;

// Schreibt rohe PCM-Daten als WAV (Ganzzahl oder 32-Bit-Fließkomma) und
// misst nebenbei den Spitzenpegel.
class WavWriter : public QIODevice {
    Q_OBJECT
public:
    bool create(const QString& path, int sampleRate, int channels, int bitsPerSample, bool isFloat);
    void finish();                    // Größen im Kopf eintragen, Datei schließen
    qint64 frames() const { return m_dataBytes / m_frameBytes; }
    double takePeak();                // Spitzenpegel seit dem letzten Abruf (0..1)

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* data, qint64 len) override;

private:
    QFile m_file;
    int m_bits = 16, m_channels = 2, m_frameBytes = 4;
    bool m_float = false;
    qint64 m_dataBytes = 0;
    double m_peak = 0.0;
};

class MicRecorder : public QObject {
    Q_OBJECT
public:
    explicit MicRecorder(QObject* parent = nullptr);
    ~MicRecorder() override;
    bool start(const QAudioDevice& device, const QString& path, QString* error);
    QString stop();                   // gibt den Dateipfad zurück
    double takePeak() { return m_writer.takePeak(); }
    double seconds() const;

private:
    QAudioSource* m_source = nullptr;
    WavWriter m_writer;
    QString m_path;
    int m_rate = 48000;
};
