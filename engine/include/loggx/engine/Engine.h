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
    // Zeichnet einen Clip zum Zeitpunkt t in ein (vormultipliziertes) Ziel.
    void drawClip(const Clip& clip, TimeUs t, int w, int h, std::vector<std::uint8_t>& dst,
                  double extraOpacity);
    void mixTransition(TransitionType type, double p, int w, int h);

    std::shared_ptr<const Project> m_project;
    std::map<std::string, std::unique_ptr<Source>> m_sources;   // nach Clip-id
    std::map<std::string, TitleCache> m_titleCache;             // nach Clip-id
    std::map<std::string, std::shared_ptr<Lut3D>> m_luts;       // nach Pfad
    TitleRasterizer m_titles;
    std::vector<std::uint8_t> m_layer;
    std::vector<std::uint8_t> m_bufA, m_bufB;   // Ebenen für Übergänge
};

// Prüft, ob eine .cube-Datei als 3D-LUT lesbar ist.
bool validateLut(const std::string& path, std::string* error = nullptr);

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

// Mastering-Assistent: Lautheit messen (LUFS) und auf ein Ziel bringen
enum class MasteringPreset { Off = 0, Streaming = 1, Club = 2, Podcast = 3 };   // −14 / −9 / −16 LUFS
struct MasteringResult {
    double lufsBefore = -70, lufsAfter = -70, gainDb = 0, maxReductionDb = 0;
};
double measureLufs(const std::vector<float>& stereo, int sampleRate);
MasteringResult masterAudio(std::vector<float>& stereo, int sampleRate, MasteringPreset preset);

struct RenderResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string videoEncoder;
    std::int64_t frames = 0;
    MasteringResult mastering;   // nur Audio-Export mit Mastering
};

// Rendert das ganze Projekt in eine Datei. settings.fps/sampleRate werden
// aus dem Projekt übernommen. progress erhält Werte von 0 bis 1.
RenderResult renderProject(const Project& project, ExportSettings settings,
                           const std::function<void(double)>& progress = {},
                           const std::atomic<bool>* cancel = nullptr,
                           TitleRasterizer titles = {});

// Rendert nur den Ton (WAV, FLAC, MP3, M4A, OGG, Opus – nach Dateiendung).
// tail: nach dem Projektende weiterrendern, bis Hall und Ausklang verstummt
// sind (höchstens 5 s).
RenderResult renderAudio(const Project& project, ExportSettings settings,
                         const std::function<void(double)>& progress = {},
                         const std::atomic<bool>* cancel = nullptr, bool tail = true);

// Rendert jede klingende Spur einzeln in den Ordner `folder` (Dateiname
// "01 Spurname.<endung>"). files erhält die geschriebenen Pfade.
RenderResult renderStems(const Project& project, const std::string& folder,
                         const std::string& extension, int audioBits,
                         std::vector<std::string>* files = nullptr,
                         const std::function<void(double)>& progress = {},
                         const std::atomic<bool>* cancel = nullptr);

// Tempo-Erkennung für Musik (Onset-Autokorrelation).
struct BeatInfo {
    double bpm = 0.0;          // 0 = nicht erkannt
    TimeUs firstBeat = 0;      // Lage des ersten Schlags im Material
    double confidence = 0.0;   // 0..1
};
BeatInfo detectBeats(const std::string& path, TimeUs maxDuration = 120 * kSecond, double minBpm = 70.0,
                     double maxBpm = 180.0);
// Tempo-Bereich wie in DJ-Programmen: das Ergebnis wird durch Verdoppeln/
// Halbieren hineingefaltet (z. B. 70–180 BPM).
BeatInfo detectBeatsFromSamples(const float* mono, std::size_t count, int sampleRate, double minBpm = 70.0,
                                double maxBpm = 180.0);

// ---- Tonart
struct KeyInfo {
    int tonic = -1;          // 0 = C … 11 = H, -1 = unbekannt
    bool minor = false;
    double confidence = 0.0; // 0..1
};
KeyInfo detectKey(const std::string& path, TimeUs maxDuration = 120 * kSecond);
KeyInfo detectKeyFromSamples(const float* mono, std::size_t count, int sampleRate);
// "a-Moll", "Fis-Dur" …
std::string keyName(int tonic, bool minor);

// ---- Stimme entfernen / isolieren (ohne KI): trennt, was in der Mitte des
// Stereobilds liegt und klingt wie Gesang, vom Rest. Instrumental + Gesang
// ergeben zusammen wieder genau das Original.
struct SeparationResult {
    bool ok = false;
    std::string error;
    bool mono = false;       // Mono-Quelle: so nicht trennbar
    double vocalShare = 0.0; // Anteil der Energie im Gesang
};
SeparationResult separateVocals(const std::string& input, const std::string& vocalsPath,
                                const std::string& instrumentalPath, int sampleRate = 48000,
                                const std::function<void(double)>& progress = {},
                                const std::atomic<bool>* cancel = nullptr);
// ---- KI-Stemtrennung (MDX-Net über ONNX Runtime). Modelle werden bei
// Bedarf heruntergeladen (siehe url/sha256) und lokal gespeichert.
struct StemModelInfo {
    std::string id, name, file, url, sha256, stem;
    std::int64_t bytes = 0;
    int nFft = 0, dimF = 0, dimTPow = 0;
    double compensate = 1.0;
};
const std::vector<StemModelInfo>& stemModels();
const StemModelInfo* findStemModel(const std::string& id);
bool aiSeparationAvailable();   // mit ONNX Runtime gebaut?
// Modell auf Stereo-Audio (44,1 kHz, verschachtelt) anwenden → Stem.
bool runStemModel(const StemModelInfo& model, const std::string& modelPath, const std::vector<float>& stereo44k,
                  std::vector<float>& stem, bool denoise = false, const std::function<void(double)>& progress = {},
                  const std::atomic<bool>* cancel = nullptr, std::string* error = nullptr);
struct StemJob {
    const StemModelInfo* model = nullptr;
    std::string modelPath;
    std::string outputPath;
};
// Datei trennen: jeder Job schreibt einen Stem als WAV, restPath bekommt den
// Rest (Original minus alle Stems, z. B. „Instrumental“).
SeparationResult separateStemsAi(const std::string& input, const std::vector<StemJob>& jobs, const std::string& restPath,
                                 bool denoise = false, const std::function<void(double)>& progress = {},
                                 const std::atomic<bool>* cancel = nullptr);

// Eingebaute Stem-Engine ohne KI (sofort, kein Download): Gesang über das
// Stereobild, Schlagzeug/Tonales per HPSS, Bass = tonal unter ~120 Hz.
SeparationResult separateStemsDsp(const std::string& input, const std::string& vocalsPath, const std::string& drumsPath,
                                  const std::string& bassPath, const std::string& instPath,
                                  const std::function<void(double)>& progress = {},
                                  const std::atomic<bool>* cancel = nullptr);
bool splitHarmonicPercussive(const std::vector<float>& stereo, int sampleRate, std::vector<float>& drums,
                             std::vector<float>& bass, const std::function<void(double)>& progress = {},
                             const std::atomic<bool>* cancel = nullptr);

// Für Tests: arbeitet auf Stereo-Samples im Speicher.
SeparationResult separateVocalsBuffer(const std::vector<float>& stereo, int sampleRate,
                                      std::vector<float>& vocals, std::vector<float>& instrumental,
                                      const std::function<void(double)>& progress = {},
                                      const std::atomic<bool>* cancel = nullptr);

// Spielt Noten live (Computer- oder MIDI-Tastatur). Thread-sicher: Noten
// kommen aus der Oberfläche oder dem MIDI-Thread, gerendert wird im
// Audio-Thread.
class LiveSynth {
public:
    void setSampleRate(int rate) { m_rate = rate; }
    int sampleRate() const { return m_rate; }
    void noteOn(int pitch, double velocity, InstrumentType type);
    void noteOff(int pitch);
    void allNotesOff();
    int activeVoices();
    // Addiert nichts, sondern schreibt (überschreibt) Stereo-Float.
    void render(float* interleaved, std::size_t frames);

private:
    struct Voice {
        int pitch;
        double velocity;
        InstrumentType type;
        std::int64_t pos = 0;     // Samples seit dem Anschlag
        std::int64_t held = -1;   // Haltedauer, -1 solange gedrückt
    };
    std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::vector<float> m_mono;
    int m_rate = 48000;
};

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
