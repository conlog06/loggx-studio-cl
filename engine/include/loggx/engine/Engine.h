// LoggX Studio – Render-Engine: Bildkomposition, Audio-Mix und Export
// eines Projekts. Vorschau und Export benutzen exakt denselben Code.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "loggx/core/Project.h"
#include "loggx/media/Media.h"

namespace loggx {

// Setzt alle Videospuren für einen Zeitpunkt zu einem RGBA-Bild zusammen
// (CPU). Nicht thread-sicher: pro Thread eine eigene Instanz verwenden.
class Compositor {
public:
    Compositor();
    ~Compositor();

    void setProject(std::shared_ptr<const Project> project);
    // Schreibt w*h*4 Bytes, deckend, schwarzer Hintergrund.
    void render(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba);

private:
    struct Source;
    std::shared_ptr<const Project> m_project;
    std::map<std::string, std::unique_ptr<Source>> m_sources;   // nach Clip-id
    std::vector<std::uint8_t> m_layer;
};

// Mischt alle Audioquellen (Audiospuren und Ton der Videoclips) zu
// Stereo-Float. Thread-sicher, damit der Audio-Thread rendern kann, während
// die Oberfläche das Projekt austauscht.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    void setProject(std::shared_ptr<const Project> project);
    // position: Timeline-Position in Samples (Abtastrate des Projekts).
    void render(std::int64_t position, float* interleaved, std::size_t frames);

    // true, wenn Pitch-Shift und Time-Stretch einkompiliert sind.
    static bool hasStretcher();

private:
    struct Source;
    std::mutex m_mutex;
    std::shared_ptr<const Project> m_project;
    std::map<std::string, std::unique_ptr<Source>> m_sources;
    std::vector<float> m_scratch;
};

struct RenderResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string videoEncoder;
    std::int64_t frames = 0;
};

// Rendert das ganze Projekt in eine Datei. settings.fps/sampleRate werden
// aus dem Projekt übernommen. progress erhält Werte von 0 bis 1.
RenderResult renderProject(const Project& project, ExportSettings settings,
                           const std::function<void(double)>& progress = {},
                           const std::atomic<bool>* cancel = nullptr);

}  // namespace loggx
