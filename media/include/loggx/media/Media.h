// LoggX Studio – Medienzugriff über FFmpeg: Analyse, Video-/Audio-Decoder
// und Export. Die FFmpeg-Typen bleiben hinter Impl-Zeigern verborgen.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "loggx/core/Project.h"

namespace loggx {

// Liest Dauer, Auflösung und Spuren einer Datei. Füllt alle Felder von
// MediaAsset außer id.
bool probeMedia(const std::string& path, MediaAsset& out, std::string* error = nullptr);

// Liefert das Bild zu einem beliebigen Zeitpunkt. Sequenzielles Abspielen
// dekodiert nur vorwärts; Sprünge lösen einen Seek auf das Keyframe davor aus.
class VideoReader {
public:
    VideoReader();
    ~VideoReader();
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);
    bool isOpen() const;
    bool hasAlpha() const;
    // Schreibt w*h*4 Bytes RGBA (nicht vormultipliziert) nach `rgba`.
    bool frameAt(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Liefert Stereo-Float-Samples (interleaved) in der gewünschten Abtastrate.
class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    bool open(const std::string& path, int sampleRate, std::string* error = nullptr);
    bool isOpen() const;
    void seek(TimeUs t);
    // Gibt die Anzahl gelesener Frames zurück; weniger als angefordert = Dateiende.
    std::size_t read(float* interleaved, std::size_t frames);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

struct ExportSettings {
    std::string path;
    int width = 1920;
    int height = 1080;
    int fpsNum = 30;
    int fpsDen = 1;
    int sampleRate = 48000;
    std::int64_t videoBitrate = 0;   // 0 = automatisch aus Auflösung und Qualität
    enum class Codec { H264, H265 } codec = Codec::H264;
    int quality = 1;                 // 0 klein, 1 normal, 2 hoch, 3 sehr hoch
    int audioBitrate = 192000;
    bool audio = true;
    // Leer = automatisch (Software-Encoder, sonst Hardware-Encoder).
    std::string videoEncoder;
};

// Schreibt eine MP4/MOV/MKV-Datei (Container nach Dateiendung) mit H.264
// oder H.265 und AAC.
class Exporter {
public:
    Exporter();
    ~Exporter();
    Exporter(const Exporter&) = delete;
    Exporter& operator=(const Exporter&) = delete;

    bool open(const ExportSettings& settings, std::string* error = nullptr);
    bool writeVideoFrame(const std::uint8_t* rgba);   // width*height*4 Bytes
    bool writeAudio(const float* interleaved, std::size_t frames);
    bool finish(std::string* error = nullptr);
    std::string videoEncoderName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace loggx
