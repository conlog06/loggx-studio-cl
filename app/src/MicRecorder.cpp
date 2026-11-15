#include "MicRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QAudioFormat>
#include <QAudioSource>
#include <QtEndian>

namespace {

void put32(char* p, quint32 v) { qToLittleEndian(v, p); }
void put16(char* p, quint16 v) { qToLittleEndian(v, p); }

}  // namespace

bool WavWriter::create(const QString& path, int sampleRate, int channels, int bitsPerSample, bool isFloat) {
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    m_bits = bitsPerSample;
    m_channels = channels;
    m_float = isFloat;
    m_frameBytes = channels * bitsPerSample / 8;
    m_dataBytes = 0;
    m_peak = 0.0;
    char header[44] = {};
    std::memcpy(header, "RIFF", 4);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    put32(header + 16, 16);
    put16(header + 20, isFloat ? 3 : 1);
    put16(header + 22, static_cast<quint16>(channels));
    put32(header + 24, static_cast<quint32>(sampleRate));
    put32(header + 28, static_cast<quint32>(sampleRate * m_frameBytes));
    put16(header + 32, static_cast<quint16>(m_frameBytes));
    put16(header + 34, static_cast<quint16>(bitsPerSample));
    std::memcpy(header + 36, "data", 4);
    m_file.write(header, 44);
    return open(QIODevice::WriteOnly);
}

qint64 WavWriter::writeData(const char* data, qint64 len) {
    const qint64 written = m_file.write(data, len);
    if (written <= 0) return written;
    m_dataBytes += written;
    // Spitzenpegel (jede vierte Probe genügt für die Anzeige)
    const int bytes = m_bits / 8;
    const qint64 count = written / bytes;
    double peak = m_peak;
    for (qint64 i = 0; i < count; i += 4) {
        const char* p = data + i * bytes;
        double v = 0.0;
        if (m_float) {
            float f;
            std::memcpy(&f, p, 4);
            v = f;
        } else if (bytes == 2) {
            v = qFromLittleEndian<qint16>(p) / 32768.0;
        } else if (bytes == 4) {
            v = qFromLittleEndian<qint32>(p) / 2147483648.0;
        } else if (bytes == 1) {
            v = (static_cast<unsigned char>(*p) - 128) / 128.0;
        }
        peak = std::max(peak, std::abs(v));
    }
    m_peak = peak;
    return written;
}

double WavWriter::takePeak() {
    const double p = std::min(1.0, m_peak);
    m_peak = 0.0;
    return p;
}

void WavWriter::finish() {
    if (!m_file.isOpen()) return;
    char size[4];
    put32(size, static_cast<quint32>(36 + m_dataBytes));
    m_file.seek(4);
    m_file.write(size, 4);
    put32(size, static_cast<quint32>(m_dataBytes));
    m_file.seek(40);
    m_file.write(size, 4);
    m_file.close();
    close();
}

// ------------------------------------------------------------ MicRecorder

MicRecorder::MicRecorder(QObject* parent) : QObject(parent) {}

MicRecorder::~MicRecorder() { stop(); }

bool MicRecorder::start(const QAudioDevice& device, const QString& path, QString* error) {
    if (device.isNull()) {
        if (error) *error = tr("Kein Mikrofon gefunden");
        return false;
    }
    // Bevorzugt 48 kHz Stereo/Mono, sonst das Format des Geräts.
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(std::min(2, std::max(1, device.maximumChannelCount())));
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) format = device.preferredFormat();
    int bits = 16;
    bool isFloat = false;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8: bits = 8; break;
    case QAudioFormat::Int16: bits = 16; break;
    case QAudioFormat::Int32: bits = 32; break;
    case QAudioFormat::Float: bits = 32; isFloat = true; break;
    default:
        if (error) *error = tr("Das Mikrofon liefert ein unbekanntes Format");
        return false;
    }
    if (!m_writer.create(path, format.sampleRate(), format.channelCount(), bits, isFloat)) {
        if (error) *error = tr("Datei kann nicht geschrieben werden: %1").arg(path);
        return false;
    }
    m_path = path;
    m_rate = format.sampleRate();
    m_source = new QAudioSource(device, format, this);
    m_source->start(&m_writer);
    if (m_source->error() != QAudio::NoError) {
        if (error) *error = tr("Das Mikrofon lässt sich nicht öffnen");
        stop();
        return false;
    }
    return true;
}

double MicRecorder::seconds() const { return static_cast<double>(m_writer.frames()) / m_rate; }

QString MicRecorder::stop() {
    if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
    }
    m_writer.finish();
    return m_path;
}
