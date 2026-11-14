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

struct Lut3D;

// Zeichnet einen Titel als RGBA-Bild (transparent, Größe w×h). Die Engine
// selbst kennt keine Schriften; die Oberfläche liefert dafür Qt.
using TitleRasterizer =
    std::function<void(const Title& title, int w, int h, std::vector<std::uint8_t>& rgba)>;

// Setzt alle Videospuren für einen Zeitpunkt zu einem RGBA-Bild zusammen
// (CPU, mehrere Kerne). Nicht thread-sicher: pro Thread eine Instanz.
class Compositor {
public:
    Compositor();
    ~Compositor();

    void setProject(std::shared_ptr<const Project> project);
    void setTitleRasterizer(TitleRasterizer rasterizer) { m_titles = std::move(rasterizer); }
    // Schreibt w*h*4 Bytes, deckend, schwarzer Hintergrund.
    void render(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba);

private:
    struct Source;
    struct TitleCache {
        std::string key;
        std::vector<std::uint8_t> rgba;
    };
    std::shared_ptr<Lut3D> lut(const std::string& path);

    std::shared_ptr<const Project> m_project;
    std::map<std::string, std::unique_ptr<Source>> m_sources;   // nach Clip-id
    std::map<std::string, TitleCache> m_titleCache;             // nach Clip-id
    std::map<std::string, std::shared_ptr<Lut3D>> m_luts;       // nach Pfad
    TitleRasterizer m_titles;
    std::vector<std::uint8_t> m_layer;
    std::vector<float> m_maskRow;
};

// Pegel für die Anzeige im Mixer (Spitzenwert seit dem letzten Abholen).
struct MeterLevels {
    std::map<std::string, float> tracks;   // nach Spur-id, linear 0..1+
    float master = 0.0f;
    float reduction = 0.0f;                // Pegelreduktion des Limiters in dB
};

// Mischt alle Audioquellen (Audiospuren, Ton der Videoclips und
// Instrumentspuren) zu Stereo-Float, mit Spureffekten und Master-Limiter.
// Thread-sicher, damit der Audio-Thread rendern kann, während die
// Oberfläche das Projekt austauscht.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    void setProject(std::shared_ptr<const Project> project);
    // position: Timeline-Position in Samples (Abtastrate des Projekts).
    void render(std::int64_t position, float* interleaved, std::size_t frames);
    MeterLevels takeLevels();

    // true, wenn Pitch-Shift und Time-Stretch einkompiliert sind.
    static bool hasStretcher();

private:
    struct Source;
    struct TrackState;
    std::mutex m_mutex;
    std::shared_ptr<const Project> m_project;
    std::map<std::string, std::unique_ptr<Source>> m_sources;
    std::map<std::string, std::unique_ptr<TrackState>> m_trackStates;
    std::vector<float> m_scratch, m_trackBuf;
    std::int64_t m_expectedPosition = -1;
    float m_limiterGain = 1.0f;
    MeterLevels m_levels;
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
                           const std::atomic<bool>* cancel = nullptr,
                           TitleRasterizer titles = {});

// Klangerzeuger der Instrumentspuren (zustandslos, damit jede Stelle der
// Timeline direkt berechnet werden kann).
namespace synth {
// Addiert eine Note ab Sample `from` (relativ zum Notenanfang) in `out`.
// held: Dauer bis zum Loslassen in Samples.
void renderNote(InstrumentType type, int pitch, double velocity, std::int64_t held,
                std::int64_t from, float* out, std::size_t frames, int sampleRate);
// Wie lange eine Note nach dem Loslassen noch klingt (Sekunden).
double releaseTime(InstrumentType type);
}  // namespace synth

}  // namespace loggx
