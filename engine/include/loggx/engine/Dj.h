// DJ: zwei Decks mit Mixer – Tempo, Sync, Key Lock, Hot Cues, Loops,
// 3-Band-EQ, Filter, Stems (Gesang/Instrumental/Bass/Schlagzeug), Effekte.
// Alle Einstellungen sind thread-sicher; render() läuft im Audio-Thread.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace loggx {

enum class DjStem { Vocals = 0, Drums = 1, Bass = 2, Inst = 3 };

struct DjTrack {
    std::string path, title, artist;
    int sampleRate = 48000;
    std::vector<float> mix;                    // Stereo, verschachtelt
    std::array<std::vector<float>, 4> stems;   // optional, siehe DjStem
    bool hasStems = false;
    double bpm = 0.0;                          // 0 = unbekannt
    double firstBeat = 0.0;                    // Sekunden
    int key = -1;
    bool minor = false;
    // Wellenform: je Spalte (1/150 s) Pegel gesamt, Tiefen, Mitten, Höhen (0..255)
    static constexpr int kWaveRate = 150;
    std::vector<std::array<std::uint8_t, 4>> wave;

    std::size_t frames() const { return mix.size() / 2; }
    double duration() const { return static_cast<double>(frames()) / sampleRate; }
    double beatLength() const { return bpm > 0 ? 60.0 / bpm : 0.0; }
};

// Datei laden (dekodieren, Tempo/Tonart erkennen, Wellenform berechnen).
// stemFiles: optional vier WAV-Dateien in der Reihenfolge von DjStem.
std::shared_ptr<DjTrack> loadDjTrack(const std::string& path, int sampleRate, std::string* error = nullptr,
                                     const std::array<std::string, 4>* stemFiles = nullptr,
                                     const std::atomic<bool>* cancel = nullptr);
void computeDjWaveform(DjTrack& track);

// Aufbau des Tracks (wie die Phrasen-Anzeige in DJ-Programmen): Abschnitte zu
// 8 Takten ab dem Raster, eingeteilt nach Energie und Bass.
enum class DjPhraseKind { Intro, Up, Chorus, Down, Outro };
struct DjPhrase {
    double start = 0, end = 0;   // Sekunden
    DjPhraseKind kind = DjPhraseKind::Intro;
};
std::vector<DjPhrase> analyzeDjPhrases(const DjTrack& track, double bpm, double firstBeat);

// Camelot-Schreibweise der Tonart (z. B. „8A“ für a-Moll)
std::string camelotKey(int tonic, bool minor);

// Effekte (Beat FX): Zeitangaben in Schlägen des Master- bzw. Ziel-Decks
enum class DjFx { Echo = 0, Reverb = 1, Brake = 2, PingPong = 3, Flanger = 4, Phaser = 5, Trans = 6, Roll = 7, Filter = 8, Noise = 9, Crush = 10, Backspin = 11 };
constexpr int kDjFxCount = 12;

class DjMixer {
public:
    explicit DjMixer(int sampleRate = 48000);
    ~DjMixer();
    int sampleRate() const { return m_rate; }

    // ---- Deck (0 oder 1)
    void load(int deck, std::shared_ptr<const DjTrack> track);
    std::shared_ptr<const DjTrack> track(int deck) const;
    void play(int deck);
    void pause(int deck);
    void togglePlay(int deck);
    bool playing(int deck) const;
    // CDJ-Verhalten: angehalten → Cue an die Position setzen; läuft →
    // zurück zum Cue und anhalten.
    void cue(int deck);
    double cuePoint(int deck) const;
    void seek(int deck, double seconds);
    double position(int deck) const;            // Sekunden
    void setTempo(int deck, double percent);     // −50 … +50
    double tempo(int deck) const;
    double currentBpm(int deck) const;
    void setKeyLock(int deck, bool on);
    bool keyLock(int deck) const;
    void sync(int deck);                         // Tempo + Phase an das andere Deck
    void setSyncLock(int deck, bool on);         // folgt dem anderen Deck dauerhaft
    bool syncLock(int deck) const;
    int masterDeck() const;
    void setQuantize(bool on);
    bool quantize() const;

    void setHotCue(int deck, int index);         // index 0..7
    void jumpHotCue(int deck, int index);        // leer → setzen
    void clearHotCue(int deck, int index);
    void setHotCuePoint(int deck, int index, double seconds);   // z. B. aus der Bibliothek
    double hotCue(int deck, int index) const;    // −1 = leer

    void setLoop(int deck, double beats);        // ab Position (Quantize: ab Schlag)
    void exitLoop(int deck);
    bool looping(int deck) const;
    double loopBeats(int deck) const;
    void beatJump(int deck, double beats);

    // Beatgrid des Decks (startet mit dem der Spur; z. B. BPM ×2, Grid verschieben)
    void setBeatGrid(int deck, double bpm, double firstBeat);
    double gridBpm(int deck) const;
    double gridFirstBeat(int deck) const;

    // Jog-Rad: oben berührt = Scratchen (Position folgt dem Rad),
    // am Rand gedreht = kurz schneller/langsamer (Bend klingt ab)
    void setScratch(int deck, bool on);
    bool scratching(int deck) const;
    void jogScratch(int deck, double seconds);
    void jogBend(int deck, double amount);

    // Kopfhörer: Vorhören (vor dem Fader), Mischung Vorhören↔Master
    void setPfl(int deck, bool on);
    bool pfl(int deck) const;
    void setHeadphones(double mix, double volume);   // mix 0 = nur Vorhören, 1 = Master

    // Key Shift in Halbtönen (−12…12), Tempo bleibt
    void setKeyShift(int deck, int semitones);
    int keyShift(int deck) const;

    // Sampler: 8 Plätze, Stereo verschachtelt in der Mixer-Abtastrate
    static constexpr int kSamplerSlots = 8;
    void setSample(int slot, std::shared_ptr<const std::vector<float>> stereo);
    bool hasSample(int slot) const;
    void triggerSample(int slot);
    void stopSample(int slot);
    bool samplePlaying(int slot) const;
    void setSamplerVolume(double v);
    double samplerVolume() const;

    void setStem(int deck, DjStem stem, bool on);
    bool stem(int deck, DjStem stem) const;
    // Stufenlose Stem-Lautstärke 0..1 (z. B. CFX-Regler); wirkt nur, wenn der Stem an ist
    void setStemLevel(int deck, DjStem stem, double level);
    double stemLevel(int deck, DjStem stem) const;

    // EQ und Filter: −1 … +1 (−1 = ausgeschaltet/Kill, 0 = neutral, +1 = +6 dB)
    void setEq(int deck, int band, double value); // 0 Tiefen, 1 Mitten, 2 Höhen
    double eq(int deck, int band) const;
    void setFilter(int deck, double value);      // <0 Tiefpass, >0 Hochpass
    double filter(int deck) const;
    void setTrim(int deck, double gain);         // 0 … 2
    void setFader(int deck, double value);       // 0 … 1
    double fader(int deck) const;
    double level(int deck) const;                // Pegel (Spitze, 0..1)

    // ---- Mixer
    void setCrossfader(double value);            // −1 = nur A, +1 = nur B
    double crossfader() const;
    // Crossfader-Kurve: 0 weich (gleiche Leistung), 1 Mix (Mitte beide voll), 2 Cut (scharf)
    void setCrossfaderCurve(int curve);
    int crossfaderCurve() const;
    // Automatischer Übergang zum anderen Deck: startet auf dem nächsten Takt,
    // dauert „beats“ Schläge. Stil: 0 Blend, 1 Bass-Tausch, 2 Filter, 3 Echo-Out
    bool startTransition(int toDeck, double beats, int style, bool quantize = true);
    void cancelTransition();
    double transitionProgress() const;   // −1 = keiner, 0…1 (vor dem Start: 0)
    int transitionTarget() const;        // Ziel-Deck oder −1
    // Phasenabweichung des Decks zum Master in Schlägen (−0,5…0,5), NaN ohne Bezug
    double phaseOffset(int deck) const;
    void setMasterVolume(double value);          // 0 … 1,5
    double masterLevel() const;

    // ---- Effekt (eine FX-Einheit wie am Mixer)
    void setFx(DjFx type, double beats, double wet, int target);   // target 0 = A, 1 = B, 2 = Master
    void setFxOn(bool on);
    bool fxOn() const;

    // Mitschnitt: bekommt jeden fertigen Master-Block
    void setRecorder(std::function<void(const float*, std::size_t)> recorder);

    // Audio-Thread: Stereo, verschachtelt; cue (optional) bekommt den
    // Kopfhörer-Mix ebenfalls als Stereo
    void render(float* out, std::size_t frames, float* cue = nullptr);

private:
    struct Deck;
    void applySync(Deck& d, const Deck& other);
    double snapToBeat(const Deck& d, double seconds) const;
    mutable std::mutex m_mutex;
    std::array<std::unique_ptr<Deck>, 2> m_decks;
    int m_rate;
    int m_master = 0;
    bool m_quantize = true;
    double m_cross = 0.0, m_masterVol = 1.0, m_masterLevel = 0.0;
    double m_crossGa = -1, m_crossGb = -1;   // zuletzt verwendete Pegel (für weiche Rampe)
    int m_xfCurve = 0;
    struct Transition {
        bool active = false, started = false;
        int from = 0, to = 1, style = 0;
        double beats = 16, waitFrames = 0, elapsed = 0, total = 1;
        double fromLow = 0, toLow = 0, fromFilt = 0;
        bool fxWasOn = false;
    } m_trans;
    void runTransition(std::size_t frames);
    struct Fx;
    std::unique_ptr<Fx> m_fx;
    std::function<void(const float*, std::size_t)> m_recorder;
    std::vector<float> m_bufA, m_bufB, m_cueBus;
    struct Voice {
        std::shared_ptr<const std::vector<float>> data;
        std::size_t pos = 0;
        bool playing = false;
    };
    std::array<Voice, kSamplerSlots> m_samples;
    double m_samplerVol = 0.8;
    double m_headMix = 0.0, m_headVol = 1.0;
};

}  // namespace loggx

namespace loggx {
// Eingebaute Sampler-Klänge (synthetisch erzeugt, frei von Rechten):
// 0 Air Horn, 1 Sirene, 2 Riser, 3 Downlifter, 4 Laser, 5 Sub Drop, 6 Clap, 7 Scratch
std::vector<float> synthDjSample(int kind, int sampleRate);
const char* djSampleName(int kind);
}  // namespace loggx
