#include "loggx/engine/Dj.h"

#include <algorithm>
#include <cmath>

#include "AudioFx.h"
#include "loggx/engine/Engine.h"
#include "loggx/media/Media.h"

#ifdef LOGGX_WITH_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace loggx {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::string camelotKey(int tonic, bool minor) {
    static const int major[12] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
    static const int minorK[12] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
    if (tonic < 0 || tonic > 11) return {};
    return std::to_string(minor ? minorK[tonic] : major[tonic]) + (minor ? "A" : "B");
}

// ------------------------------------------------------------ Laden

void computeDjWaveform(DjTrack& t) {
    const std::size_t frames = t.frames();
    const int per = std::max(1, t.sampleRate / DjTrack::kWaveRate);
    const std::size_t cols = (frames + per - 1) / static_cast<std::size_t>(per);
    t.wave.assign(cols, {0, 0, 0, 0});
    // Einfache Bandaufteilung: Tiefpass 200 Hz, Hochpass ab 2,5 kHz, Rest = Mitten
    const double aLow = 1.0 - std::exp(-2.0 * kPi * 200.0 / t.sampleRate);
    const double aHigh = 1.0 - std::exp(-2.0 * kPi * 2500.0 / t.sampleRate);
    double lp = 0, lp2 = 0;
    std::vector<std::array<float, 4>> peaks(cols, {0, 0, 0, 0});
    float maxAll = 1e-6f;
    for (std::size_t i = 0; i < frames; ++i) {
        const double x = 0.5 * (t.mix[i * 2] + t.mix[i * 2 + 1]);
        lp += aLow * (x - lp);
        lp2 += aHigh * (x - lp2);
        const double high = x - lp2, low = lp, mid = lp2 - lp;
        auto& p = peaks[i / static_cast<std::size_t>(per)];
        p[0] = std::max(p[0], static_cast<float>(std::abs(x)));
        p[1] = std::max(p[1], static_cast<float>(std::abs(low)));
        p[2] = std::max(p[2], static_cast<float>(std::abs(mid)));
        p[3] = std::max(p[3], static_cast<float>(std::abs(high)));
        maxAll = std::max(maxAll, p[0]);
    }
    for (std::size_t c = 0; c < cols; ++c)
        for (int b = 0; b < 4; ++b)
            t.wave[c][static_cast<std::size_t>(b)] =
                static_cast<std::uint8_t>(std::clamp(peaks[c][static_cast<std::size_t>(b)] / maxAll * 255.0f, 0.0f, 255.0f));
}

std::shared_ptr<DjTrack> loadDjTrack(const std::string& path, int sampleRate, std::string* error,
                                     const std::array<std::string, 4>* stemFiles, const std::atomic<bool>* cancel) {
    auto readAll = [&](const std::string& file, std::vector<float>& out) {
        AudioReader reader;
        if (!reader.open(file, sampleRate, error)) return false;
        std::vector<float> buf(16384 * 2);
        out.clear();
        for (;;) {
            if (cancel && cancel->load()) return false;
            const std::size_t got = reader.read(buf.data(), 16384);
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * 2));
            if (got < 16384) break;
            if (out.size() > static_cast<std::size_t>(sampleRate) * 2 * 60 * 60) break;   // höchstens 1 Stunde
        }
        return true;
    };
    auto t = std::make_shared<DjTrack>();
    t->path = path;
    t->sampleRate = sampleRate;
    if (!readAll(path, t->mix) || t->mix.empty()) {
        if (error && error->empty()) *error = "Keine Tonspur";
        return nullptr;
    }
    MediaTags tags;
    if (readTags(path, tags)) {
        t->title = tags.title;
        t->artist = tags.artist;
    }
    if (t->title.empty()) {
        const std::size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::size_t dot = name.find_last_of('.');
        t->title = dot == std::string::npos ? name : name.substr(0, dot);
        // Unterstriche aus Downloads als Leerzeichen, doppelte zusammenfassen
        std::string clean;
        for (char c : t->title) {
            const char x = c == '_' ? ' ' : c;
            if (x == ' ' && (clean.empty() || clean.back() == ' ')) continue;
            clean += x;
        }
        while (!clean.empty() && clean.back() == ' ') clean.pop_back();
        if (!clean.empty()) t->title = clean;
    }
    // Analyse auf Mono (höchstens 3 Minuten aus der Mitte reichen)
    const std::size_t frames = t->frames();
    const std::size_t span = std::min<std::size_t>(frames, static_cast<std::size_t>(sampleRate) * 180);
    const std::size_t from = (frames - span) / 2;
    std::vector<float> mono(span);
    for (std::size_t i = 0; i < span; ++i) mono[i] = 0.5f * (t->mix[(from + i) * 2] + t->mix[(from + i) * 2 + 1]);
    const BeatInfo beat = detectBeatsFromSamples(mono.data(), mono.size(), sampleRate);
    if (beat.bpm > 0) {
        t->bpm = beat.bpm;
        const double len = 60.0 / beat.bpm;
        const double first = static_cast<double>(from) / sampleRate + static_cast<double>(beat.firstBeat) / 1e6;
        t->firstBeat = std::fmod(first, len);
    }
    const KeyInfo key = detectKeyFromSamples(mono.data(), mono.size(), sampleRate);
    t->key = key.tonic;
    t->minor = key.minor;
    if (stemFiles) {
        bool ok = true;
        for (int s = 0; s < 4 && ok; ++s) {
            ok = readAll((*stemFiles)[static_cast<std::size_t>(s)], t->stems[static_cast<std::size_t>(s)]);
            if (ok) t->stems[static_cast<std::size_t>(s)].resize(t->mix.size(), 0.0f);
        }
        t->hasStems = ok;
        if (!ok)
            for (auto& s : t->stems) s.clear();
    }
    computeDjWaveform(*t);
    return t;
}

// ------------------------------------------------------------ Deck

struct DjMixer::Deck {
    std::shared_ptr<const DjTrack> track;
    bool playing = false;
    double pos = 0.0;           // in Samples der Spur
    double cue = 0.0;           // Sekunden
    double tempo = 0.0;         // Prozent
    double gridBpm = 0.0;       // Beatgrid (aus der Spur, änderbar)
    double gridFirst = 0.0;     // Sekunden
    bool pfl = false;           // Vorhören (Kopfhörer)
    double bend = 0.0;          // Jog-Rad am Rand: kurz schneller/langsamer
    bool scratch = false;       // Jog-Rad oben berührt
    double scratchPending = 0.0, scratchVel = 0.0;   // Samples
    bool keyLock = false;
    int keyShift = 0;            // Halbtöne (Key-Shift-Pads, Key Sync)
    bool sync = false;
    std::array<double, 8> hot{};
    bool loop = false;
    double loopStart = 0, loopEnd = 0, loopBeats = 0;   // Samples / Schläge
    std::array<bool, 4> stemOn{true, true, true, true};
    std::array<float, 4> stemLevel{1, 1, 1, 1};
    std::array<float, 4> stemGain{1, 1, 1, 1};
    double eq[3] = {0, 0, 0};
    double designedEq[3] = {1e9, 1e9, 1e9};
    double filt = 0, designedFilt = 1e9;
    double trim = 1.0, fader = 1.0;
    // weich nachgeführte Werte (gegen Knacken beim Drehen/Schieben)
    double eqS[3] = {0, 0, 0}, filtS = 0, trimS = 1.0, fadeS = 1.0;
    double phaseCorr = 0.0;     // Phasenregelung bei Sync (±1 %)
    Biquad low, mid, high, filter;
    double level = 0.0;
    double brake = 1.0;         // Bremse: Faktor auf das Tempo
    double brakeStep = 0.0;
#ifdef LOGGX_WITH_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
#endif
    bool stretchReset = true;
    std::vector<float> inL, inR, outL, outR;

    Deck() { hot.fill(-1.0); }
    double rate() const { return (1.0 + tempo / 100.0) * brake * (1.0 + bend) * (1.0 + phaseCorr); }
    double baseRate() const { return (1.0 + tempo / 100.0) * brake * (1.0 + bend); }
    double beatLen() const { return gridBpm > 0 ? 60.0 / gridBpm : 0.0; }
    double seconds() const { return track ? pos / track->sampleRate : 0.0; }
    double bpm() const { return track && gridBpm > 0 ? gridBpm * (1.0 + tempo / 100.0) : 0.0; }

    // Ein Sample der Spur (mit Stems) an ganzzahliger Stelle, Loop beachtet
    std::size_t wrap(double i) const {
        if (loop && loopEnd > loopStart && i >= loopEnd) i = loopStart + std::fmod(i - loopStart, loopEnd - loopStart);
        return i < 0 ? std::size_t(-1) : static_cast<std::size_t>(i);
    }
    void sampleAt(std::size_t i, float& l, float& r) const {
        const DjTrack& t = *track;
        if (i >= t.frames()) {
            l = r = 0.0f;
            return;
        }
        if (t.hasStems && !(stemGain[0] == 1 && stemGain[1] == 1 && stemGain[2] == 1 && stemGain[3] == 1)) {
            l = r = 0.0f;
            for (int s = 0; s < 4; ++s) {
                const auto& st = t.stems[static_cast<std::size_t>(s)];
                l += st[i * 2] * stemGain[static_cast<std::size_t>(s)];
                r += st[i * 2 + 1] * stemGain[static_cast<std::size_t>(s)];
            }
            return;
        }
        l = t.mix[i * 2];
        r = t.mix[i * 2 + 1];
    }
    void designFilters(int rate) {
        static constexpr double freqs[3] = {250.0, 1000.0, 3500.0};
        for (int b = 0; b < 3; ++b) {
            if (eqS[b] == designedEq[b]) continue;
            const double db = eqS[b] < 0 ? 40.0 * eqS[b] : 6.0 * eqS[b];
            Biquad& f = b == 0 ? low : b == 1 ? mid : high;
            f.design(b == 0 ? Biquad::Type::LowShelf : b == 1 ? Biquad::Type::Peak : Biquad::Type::HighShelf, freqs[b], db,
                     b == 1 ? 0.6 : 0.7, rate);
            designedEq[b] = eqS[b];
        }
        if (filtS != designedFilt) {
            if (filtS < -0.02)
                filter.design(Biquad::Type::LowPass, 20000.0 * std::pow(100.0 / 20000.0, -filtS), 0, 1.0, rate);
            else if (filtS > 0.02)
                filter.design(Biquad::Type::HighPass, 20.0 * std::pow(8000.0 / 20.0, filtS), 0, 1.0, rate);
            designedFilt = filtS;
        }
    }
};

struct DjMixer::Fx {
    DjFx type = DjFx::Echo;
    float crushHold[2] = {0, 0};
    unsigned crushCount = 0;
    double beats = 0.75, wet = 0.5;
    int target = 2;
    bool on = false;
    std::vector<float> delay;
    std::size_t delayPos = 0;
    Reverb reverb;
    std::vector<float> mono, wetL, wetR;
    // weitere Effekte
    std::vector<float> roll;            // Ringpuffer (Stereo) für Roll
    std::vector<float> rollSeg;         // eingefrorenes Stück
    std::size_t rollPos = 0, rollLen = 0, rollRead = 0;
    bool rollActive = false;
    std::vector<float> flange;          // kurze Verzögerung (Stereo)
    std::size_t flangePos = 0;
    double lfo = 0.0;                    // 0…1 freilaufend
    std::array<std::array<float, 4>, 2> ap{};   // Phaser: Allpass-Zustände
    float phaserFb[2] = {0, 0};
    Biquad sweep;
    std::uint32_t noiseSeed = 22222;
    float noiseLp[2] = {0, 0};
};

DjMixer::DjMixer(int sampleRate) : m_rate(sampleRate), m_fx(std::make_unique<Fx>()) {
    for (auto& d : m_decks) d = std::make_unique<Deck>();
    m_fx->delay.assign(static_cast<std::size_t>(sampleRate) * 4 * 2, 0.0f);
    m_fx->roll.assign(static_cast<std::size_t>(sampleRate) * 4 * 2, 0.0f);
    m_fx->flange.assign(static_cast<std::size_t>(sampleRate) / 20 * 2, 0.0f);
    m_fx->reverb.prepare(sampleRate);
}

DjMixer::~DjMixer() = default;

#define LOCK std::lock_guard<std::mutex> lock(m_mutex)
#define DECK(i) (*m_decks[static_cast<std::size_t>((i) & 1)])

void DjMixer::load(int deck, std::shared_ptr<const DjTrack> track) {
    LOCK;
    Deck& d = DECK(deck);
    // Mixer-Einstellungen des Kanals bleiben beim Laden erhalten.
    const Deck keep = std::move(d);
    d = Deck();
    d.tempo = keep.tempo;
    d.keyLock = keep.keyLock;
    d.keyShift = 0;
    d.sync = keep.sync;
    for (int b = 0; b < 3; ++b) d.eq[b] = keep.eq[b];
    d.filt = keep.filt;
    d.trim = keep.trim;
    d.fader = keep.fader;
    d.pfl = keep.pfl;
    d.track = std::move(track);
    if (d.track) {
        d.gridBpm = d.track->bpm;
        d.gridFirst = d.track->firstBeat;
    }
    // Cue auf den ersten Schlag
    if (d.track && d.gridBpm > 0) d.cue = d.gridFirst;
    if (d.track) d.pos = d.cue * d.track->sampleRate;
}

std::shared_ptr<const DjTrack> DjMixer::track(int deck) const {
    LOCK;
    return DECK(deck).track;
}

void DjMixer::play(int deck) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track) return;
    if (d.pos >= d.track->frames()) d.pos = 0;
    d.playing = true;
    d.brake = 1.0;
    const Deck& other = DECK(deck + 1);
    if (!other.playing) m_master = deck & 1;
}

void DjMixer::pause(int deck) {
    LOCK;
    DECK(deck).playing = false;
    if (m_master == (deck & 1) && DECK(deck + 1).playing) m_master = (deck + 1) & 1;
}

void DjMixer::togglePlay(int deck) {
    if (playing(deck))
        pause(deck);
    else
        play(deck);
}

bool DjMixer::playing(int deck) const {
    LOCK;
    return DECK(deck).playing;
}

void DjMixer::cue(int deck) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track) return;
    if (d.playing) {
        d.playing = false;
        d.pos = d.cue * d.track->sampleRate;
    } else {
        double s = d.seconds();
        if (m_quantize) s = snapToBeat(d, s);
        d.cue = s;
        d.pos = s * d.track->sampleRate;
    }
    d.stretchReset = true;
}

double DjMixer::cuePoint(int deck) const {
    LOCK;
    return DECK(deck).cue;
}

void DjMixer::seek(int deck, double seconds) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track) return;
    d.pos = std::clamp(seconds, 0.0, d.track->duration()) * d.track->sampleRate;
    if (d.loop && (d.pos < d.loopStart || d.pos >= d.loopEnd)) d.loop = false;
    d.stretchReset = true;
}

double DjMixer::position(int deck) const {
    LOCK;
    return DECK(deck).seconds();
}

void DjMixer::setTempo(int deck, double percent) {
    LOCK;
    DECK(deck).tempo = std::clamp(percent, -50.0, 50.0);
}

double DjMixer::tempo(int deck) const {
    LOCK;
    return DECK(deck).tempo;
}

double DjMixer::currentBpm(int deck) const {
    LOCK;
    return DECK(deck).bpm();
}

void DjMixer::setKeyLock(int deck, bool on) {
    LOCK;
    DECK(deck).keyLock = on;
    DECK(deck).stretchReset = true;
}

bool DjMixer::keyLock(int deck) const {
    LOCK;
    return DECK(deck).keyLock;
}

void DjMixer::applySync(Deck& d, const Deck& other) {
    if (!d.track || !other.track || d.gridBpm <= 0 || other.gridBpm <= 0) return;
    // Halbes/doppeltes Tempo erkennen (z. B. 87 ↔ 174 BPM)
    double target = other.bpm();
    while (target / d.gridBpm > 1.5) target /= 2;
    while (target / d.gridBpm < 0.67) target *= 2;
    d.tempo = std::clamp((target / d.gridBpm - 1.0) * 100.0, -50.0, 50.0);
}

void DjMixer::sync(int deck) {
    LOCK;
    Deck& d = DECK(deck);
    const Deck& other = DECK(deck + 1);
    if (!d.track || !other.track || d.gridBpm <= 0 || other.gridBpm <= 0) return;
    applySync(d, other);
    // Phase: gleiche Stelle im Schlag wie das andere Deck
    const double lo = other.beatLen(), ls = d.beatLen();
    auto frac = [](double x) { return x - std::floor(x); };
    const double po = frac((other.seconds() - other.gridFirst) / lo);
    const double ps = frac((d.seconds() - d.gridFirst) / ls);
    double diff = po - ps;
    if (diff > 0.5) diff -= 1.0;
    if (diff < -0.5) diff += 1.0;
    d.pos = std::max(0.0, d.pos + diff * ls * d.track->sampleRate);
    d.stretchReset = true;
}

void DjMixer::setSyncLock(int deck, bool on) {
    LOCK;
    DECK(deck).sync = on;
}

bool DjMixer::syncLock(int deck) const {
    LOCK;
    return DECK(deck).sync;
}

int DjMixer::masterDeck() const {
    LOCK;
    return m_master;
}

void DjMixer::setQuantize(bool on) {
    LOCK;
    m_quantize = on;
}

bool DjMixer::quantize() const {
    LOCK;
    return m_quantize;
}

double DjMixer::snapToBeat(const Deck& d, double s) const {
    if (!d.track || d.gridBpm <= 0) return s;
    const double len = d.beatLen();
    return std::max(0.0, d.gridFirst + std::round((s - d.gridFirst) / len) * len);
}

void DjMixer::setHotCue(int deck, int index) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track || index < 0 || index > 7) return;
    d.hot[static_cast<std::size_t>(index)] = m_quantize ? snapToBeat(d, d.seconds()) : d.seconds();
}

void DjMixer::jumpHotCue(int deck, int index) {
    if (index < 0 || index > 7) return;
    if (hotCue(deck, index) < 0) {
        setHotCue(deck, index);
        return;
    }
    LOCK;
    Deck& d = DECK(deck);
    d.pos = d.hot[static_cast<std::size_t>(index)] * d.track->sampleRate;
    d.loop = false;
    d.stretchReset = true;
    if (!d.playing) d.cue = d.hot[static_cast<std::size_t>(index)];
}

void DjMixer::clearHotCue(int deck, int index) {
    LOCK;
    if (index >= 0 && index < 8) DECK(deck).hot[static_cast<std::size_t>(index)] = -1.0;
}

void DjMixer::setHotCuePoint(int deck, int index, double seconds) {
    LOCK;
    Deck& d = DECK(deck);
    if (index < 0 || index > 7) return;
    d.hot[static_cast<std::size_t>(index)] = d.track && seconds >= 0 && seconds < d.track->duration() ? seconds : -1.0;
}

double DjMixer::hotCue(int deck, int index) const {
    LOCK;
    return index >= 0 && index < 8 ? DECK(deck).hot[static_cast<std::size_t>(index)] : -1.0;
}

void DjMixer::setLoop(int deck, double beats) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track || d.gridBpm <= 0 || beats <= 0) return;
    const double len = d.beatLen() * beats;
    double start = d.seconds();
    // Gleiche Länge erneut: Loop aus; sonst neue Länge ab gleichem Start
    if (d.loop) {
        if (std::abs(d.loopBeats - beats) < 1e-9) {
            d.loop = false;
            return;
        }
        start = d.loopStart / d.track->sampleRate;
    } else if (m_quantize) {
        const double b = d.beatLen();
        start = std::max(0.0, d.gridFirst + std::floor((start - d.gridFirst) / b + 1e-6) * b);
    }
    d.loopStart = start * d.track->sampleRate;
    d.loopEnd = (start + len) * d.track->sampleRate;
    d.loopBeats = beats;
    d.loop = true;
    if (d.pos >= d.loopEnd) d.pos = d.loopStart + std::fmod(d.pos - d.loopStart, d.loopEnd - d.loopStart);
}

void DjMixer::exitLoop(int deck) {
    LOCK;
    DECK(deck).loop = false;
}

bool DjMixer::looping(int deck) const {
    LOCK;
    return DECK(deck).loop;
}

double DjMixer::loopBeats(int deck) const {
    LOCK;
    return DECK(deck).loop ? DECK(deck).loopBeats : 0.0;
}

void DjMixer::beatJump(int deck, double beats) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track || d.gridBpm <= 0) return;
    const double shift = beats * d.beatLen() * d.track->sampleRate;
    d.pos = std::clamp(d.pos + shift, 0.0, static_cast<double>(d.track->frames()));
    if (d.loop) {
        d.loopStart += shift;
        d.loopEnd += shift;
    }
    d.stretchReset = true;
}

void DjMixer::setStem(int deck, DjStem stem, bool on) {
    LOCK;
    DECK(deck).stemOn[static_cast<std::size_t>(stem)] = on;
}

bool DjMixer::stem(int deck, DjStem stem) const {
    LOCK;
    return DECK(deck).stemOn[static_cast<std::size_t>(stem)];
}

void DjMixer::setStemLevel(int deck, DjStem stem, double level) {
    LOCK;
    DECK(deck).stemLevel[static_cast<std::size_t>(stem)] = static_cast<float>(std::clamp(level, 0.0, 1.0));
}

double DjMixer::stemLevel(int deck, DjStem stem) const {
    LOCK;
    return DECK(deck).stemLevel[static_cast<std::size_t>(stem)];
}

void DjMixer::setEq(int deck, int band, double value) {
    LOCK;
    if (band >= 0 && band < 3) DECK(deck).eq[band] = std::clamp(value, -1.0, 1.0);
}

double DjMixer::eq(int deck, int band) const {
    LOCK;
    return band >= 0 && band < 3 ? DECK(deck).eq[band] : 0.0;
}

void DjMixer::setFilter(int deck, double value) {
    LOCK;
    DECK(deck).filt = std::clamp(value, -1.0, 1.0);
}

double DjMixer::filter(int deck) const {
    LOCK;
    return DECK(deck).filt;
}

void DjMixer::setTrim(int deck, double gain) {
    LOCK;
    DECK(deck).trim = std::clamp(gain, 0.0, 2.0);
}

void DjMixer::setFader(int deck, double value) {
    LOCK;
    DECK(deck).fader = std::clamp(value, 0.0, 1.0);
}

double DjMixer::fader(int deck) const {
    LOCK;
    return DECK(deck).fader;
}

double DjMixer::level(int deck) const {
    LOCK;
    return DECK(deck).level;
}

void DjMixer::setCrossfaderCurve(int curve) {
    LOCK;
    m_xfCurve = std::clamp(curve, 0, 2);
}

int DjMixer::crossfaderCurve() const {
    LOCK;
    return m_xfCurve;
}

double DjMixer::phaseOffset(int deck) const {
    LOCK;
    const Deck& d = DECK(deck);
    const Deck& m = DECK(1 - deck);
    if (!d.track || !m.track || d.gridBpm <= 0 || m.gridBpm <= 0) return std::nan("");
    auto frac = [](double v) { return v - std::floor(v); };
    double diff = frac((d.seconds() - d.gridFirst) / d.beatLen()) - frac((m.seconds() - m.gridFirst) / m.beatLen());
    if (diff > 0.5) diff -= 1.0;
    if (diff < -0.5) diff += 1.0;
    return diff;
}

bool DjMixer::startTransition(int toDeck, double beats, int style, bool quantize) {
    LOCK;
    toDeck = std::clamp(toDeck, 0, 1);
    Deck& to = DECK(toDeck);
    Deck& from = DECK(1 - toDeck);
    if (!to.track) return false;
    Transition& tr = m_trans;
    tr = {};
    tr.active = true;
    tr.from = 1 - toDeck;
    tr.to = toDeck;
    tr.style = std::clamp(style, 0, 3);
    tr.beats = std::clamp(beats, 1.0, 128.0);
    tr.fromLow = from.eq[0];
    tr.toLow = to.eq[0];
    tr.fromFilt = from.filt;
    tr.fxWasOn = m_fx->on;
    // Auf den nächsten Takt des laufenden Decks warten
    if (quantize && from.playing && from.track && from.gridBpm > 0) {
        const double bar = from.beatLen() * 4.0;
        const double ph = (from.seconds() - from.gridFirst) / bar;
        double wait = (std::ceil(ph) - ph) * bar / std::max(0.1, from.rate());
        if (wait < 0.03) wait = 0.0;
        tr.waitFrames = wait * m_rate;
    }
    if (tr.style == 1) to.eq[0] = -1.0;   // Bass des neuen Tracks zunächst raus
    return true;
}

void DjMixer::cancelTransition() {
    LOCK;
    if (!m_trans.active) return;
    Deck& from = DECK(m_trans.from);
    Deck& to = DECK(m_trans.to);
    from.eq[0] = m_trans.fromLow;
    from.filt = m_trans.fromFilt;
    to.eq[0] = m_trans.toLow;
    if (m_trans.style == 3 && !m_trans.fxWasOn) m_fx->on = false;
    m_trans.active = false;
}

double DjMixer::transitionProgress() const {
    LOCK;
    if (!m_trans.active) return -1.0;
    return m_trans.started ? std::clamp(m_trans.elapsed / m_trans.total, 0.0, 1.0) : 0.0;
}

int DjMixer::transitionTarget() const {
    LOCK;
    return m_trans.active ? m_trans.to : -1;
}

// Läuft im Audio-Thread zu Beginn jedes Blocks (Sperre ist gehalten)
void DjMixer::runTransition(std::size_t frames) {
    Transition& tr = m_trans;
    if (!tr.active) return;
    Deck& from = *m_decks[static_cast<std::size_t>(tr.from)];
    Deck& to = *m_decks[static_cast<std::size_t>(tr.to)];
    if (!tr.started) {
        tr.waitFrames -= static_cast<double>(frames);
        if (tr.waitFrames > 0) return;
        // Start: Tempo und Schlag angleichen, Sync halten, neues Deck los
        if (from.playing && from.track && to.track && to.gridBpm > 0 && from.gridBpm > 0) {
            applySync(to, from);
            to.sync = true;
            auto frac = [](double v) { return v - std::floor(v); };
            double diff = frac((from.seconds() - from.gridFirst) / from.beatLen()) - frac((to.seconds() - to.gridFirst) / to.beatLen());
            if (diff > 0.5) diff -= 1.0;
            if (diff < -0.5) diff += 1.0;
            to.pos = std::max(0.0, to.pos + diff * to.beatLen() * to.track->sampleRate);
            to.stretchReset = true;
        }
        to.playing = true;
        const double bpm = from.bpm() > 0 ? from.bpm() : (to.bpm() > 0 ? to.bpm() : 120.0);
        tr.total = std::max(0.25, tr.beats * 60.0 / bpm);
        tr.started = true;
        tr.elapsed = 0;
    } else {
        tr.elapsed += static_cast<double>(frames) / m_rate;
    }
    const double p = std::clamp(tr.elapsed / tr.total, 0.0, 1.0);
    const double fromSide = tr.from == 0 ? -1.0 : 1.0;
    auto smooth = [](double v) { return v * v * (3.0 - 2.0 * v); };
    double cross = p;
    if (tr.style == 3) cross = p < 0.6 ? 0.5 * smooth(p / 0.6) : 0.5 + 0.5 * smooth((p - 0.6) / 0.4);   // Echo-Out: Echo klingt beim Rüberblenden aus
    else cross = smooth(p);
    m_cross = fromSide - 2.0 * fromSide * cross;
    if (tr.style == 1) {   // Bass-Tausch in der Mitte (über einen Schlag)
        const double w = std::max(0.02, 1.0 / tr.beats);
        const double s = std::clamp((p - (0.5 - w / 2)) / w, 0.0, 1.0);
        from.eq[0] = tr.fromLow + (-1.0 - tr.fromLow) * s;
        to.eq[0] = -1.0 + (tr.toLow + 1.0) * s;
    } else if (tr.style == 2) {   // Hochpass-Fahrt auf dem alten Track
        from.filt = tr.fromFilt + (0.85 - tr.fromFilt) * smooth(p);
    } else if (tr.style == 3 && p >= 0.6 && !m_fx->on) {   // Echo auf dem alten Track
        m_fx->type = DjFx::Echo;
        m_fx->beats = 0.75;
        m_fx->wet = 0.7;
        m_fx->target = tr.from;
        m_fx->on = true;
    }
    if (p >= 1.0) {
        // Fertig: altes Deck stoppen und zurücksetzen
        from.playing = false;
        from.eq[0] = tr.fromLow;
        from.filt = tr.fromFilt;
        to.eq[0] = tr.toLow;
        if (tr.style == 3 && !tr.fxWasOn) m_fx->on = false;
        m_master = tr.to;
        tr.active = false;
    }
}

void DjMixer::setCrossfader(double value) {
    LOCK;
    m_cross = std::clamp(value, -1.0, 1.0);
}

double DjMixer::crossfader() const {
    LOCK;
    return m_cross;
}

void DjMixer::setMasterVolume(double value) {
    LOCK;
    m_masterVol = std::clamp(value, 0.0, 1.5);
}

double DjMixer::masterLevel() const {
    LOCK;
    return m_masterLevel;
}

void DjMixer::setFx(DjFx type, double beats, double wet, int target) {
    LOCK;
    m_fx->type = type;
    m_fx->beats = std::clamp(beats, 0.125, 8.0);
    m_fx->wet = std::clamp(wet, 0.0, 1.0);
    m_fx->target = std::clamp(target, 0, 2);
}

void DjMixer::setFxOn(bool on) {
    LOCK;
    if (on && !m_fx->on && (m_fx->type == DjFx::Brake || m_fx->type == DjFx::Backspin)) {
        // Bremse: Zieldeck über „beats“ Schläge zum Stillstand bringen;
        // Backspin: erst schnell rückwärts, dann auslaufen
        Deck& d = DECK(m_fx->target == 2 ? m_master : m_fx->target);
        const double seconds = d.track && d.gridBpm > 0 ? m_fx->beats * 60.0 / d.bpm() : m_fx->beats * 0.5;
        if (m_fx->type == DjFx::Brake) {
            d.brakeStep = 1.0 / std::max(1.0, seconds * m_rate);
        } else if (d.playing) {
            d.brake = -3.0;
            d.brakeStep = -3.0 / std::max(1.0, seconds * m_rate);
        }
    }
    if (!on) {
        std::fill(m_fx->delay.begin(), m_fx->delay.end(), 0.0f);
        m_fx->reverb.reset();
        for (auto& d : m_decks) {
            d->brake = 1.0;
            d->brakeStep = 0.0;
        }
    }
    m_fx->on = on;
}

bool DjMixer::fxOn() const {
    LOCK;
    return m_fx->on;
}

void DjMixer::setRecorder(std::function<void(const float*, std::size_t)> recorder) {
    LOCK;
    m_recorder = std::move(recorder);
}

void DjMixer::setBeatGrid(int deck, double bpm, double firstBeat) {
    LOCK;
    Deck& d = DECK(deck);
    if (!d.track) return;
    d.gridBpm = std::clamp(bpm, 0.0, 400.0);
    d.gridFirst = d.gridBpm > 0 ? std::fmod(std::max(0.0, firstBeat), 60.0 / d.gridBpm) : 0.0;
}

double DjMixer::gridBpm(int deck) const {
    LOCK;
    return DECK(deck).gridBpm;
}

double DjMixer::gridFirstBeat(int deck) const {
    LOCK;
    return DECK(deck).gridFirst;
}

void DjMixer::setPfl(int deck, bool on) {
    LOCK;
    DECK(deck).pfl = on;
}

bool DjMixer::pfl(int deck) const {
    LOCK;
    return DECK(deck).pfl;
}

void DjMixer::setHeadphones(double mix, double volume) {
    LOCK;
    m_headMix = std::clamp(mix, 0.0, 1.0);
    m_headVol = std::clamp(volume, 0.0, 1.5);
}

void DjMixer::setScratch(int deck, bool on) {
    LOCK;
    Deck& d = DECK(deck);
    d.scratch = on;
    d.scratchPending = 0.0;
    d.scratchVel = 0.0;
    d.stretchReset = true;
}

bool DjMixer::scratching(int deck) const {
    LOCK;
    return DECK(deck).scratch;
}

void DjMixer::jogScratch(int deck, double seconds) {
    LOCK;
    Deck& d = DECK(deck);
    if (d.track) d.scratchPending += seconds * d.track->sampleRate;
}

void DjMixer::jogBend(int deck, double amount) {
    LOCK;
    DECK(deck).bend = std::clamp(DECK(deck).bend + amount, -0.5, 0.5);
}

void DjMixer::setKeyShift(int deck, int semitones) {
    LOCK;
    Deck& d = DECK(deck);
    d.keyShift = std::clamp(semitones, -12, 12);
}

int DjMixer::keyShift(int deck) const {
    LOCK;
    return DECK(deck).keyShift;
}

void DjMixer::setSample(int slot, std::shared_ptr<const std::vector<float>> stereo) {
    LOCK;
    if (slot < 0 || slot >= kSamplerSlots) return;
    Voice& v = m_samples[static_cast<std::size_t>(slot)];
    v.data = std::move(stereo);
    v.pos = 0;
    v.playing = false;
}

bool DjMixer::hasSample(int slot) const {
    LOCK;
    return slot >= 0 && slot < kSamplerSlots && m_samples[static_cast<std::size_t>(slot)].data != nullptr;
}

void DjMixer::triggerSample(int slot) {
    LOCK;
    if (slot < 0 || slot >= kSamplerSlots) return;
    Voice& v = m_samples[static_cast<std::size_t>(slot)];
    if (!v.data) return;
    v.pos = 0;   // erneut drücken = von vorn
    v.playing = true;
}

void DjMixer::stopSample(int slot) {
    LOCK;
    if (slot >= 0 && slot < kSamplerSlots) m_samples[static_cast<std::size_t>(slot)].playing = false;
}

bool DjMixer::samplePlaying(int slot) const {
    LOCK;
    return slot >= 0 && slot < kSamplerSlots && m_samples[static_cast<std::size_t>(slot)].playing;
}

void DjMixer::setSamplerVolume(double v) {
    LOCK;
    m_samplerVol = std::clamp(v, 0.0, 1.5);
}

double DjMixer::samplerVolume() const {
    LOCK;
    return m_samplerVol;
}

// ------------------------------------------------------------ Audio

void DjMixer::render(float* out, std::size_t frames, float* cue) {
    LOCK;
    std::fill(out, out + frames * 2, 0.0f);
    if (cue) m_cueBus.assign(frames * 2, 0.0f);
    m_bufA.assign(frames * 2, 0.0f);
    m_bufB.assign(frames * 2, 0.0f);

    // Master bestimmen und Sync-Lock nachführen
    if (!m_decks[static_cast<std::size_t>(m_master)]->playing && m_decks[static_cast<std::size_t>(1 - m_master)]->playing)
        m_master = 1 - m_master;
    runTransition(frames);
    for (int i = 0; i < 2; ++i) {
        Deck& d = *m_decks[static_cast<std::size_t>(i)];
        const Deck& m = *m_decks[static_cast<std::size_t>(m_master)];
        d.phaseCorr = 0.0;
        if (!d.sync || i == m_master) continue;
        applySync(d, m);
        // Phasenregelung: Schläge bleiben auch über lange Übergänge übereinander
        if (d.playing && m.playing && !d.scratch && d.track && m.track && d.gridBpm > 0 && m.gridBpm > 0 && d.bend == 0.0) {
            auto frac = [](double v) { return v - std::floor(v); };
            double diff = frac((m.seconds() - m.gridFirst) / m.beatLen()) - frac((d.seconds() - d.gridFirst) / d.beatLen());
            if (diff > 0.5) diff -= 1.0;
            if (diff < -0.5) diff += 1.0;
            if (std::abs(diff) > 0.002) d.phaseCorr = std::clamp(diff * 0.25, -0.01, 0.01);
        }
    }

    for (int di = 0; di < 2; ++di) {
        Deck& d = *m_decks[static_cast<std::size_t>(di)];
        float* buf = di == 0 ? m_bufA.data() : m_bufB.data();
        // Jog-Bend klingt von selbst ab
        d.bend *= 0.8;
        if (std::abs(d.bend) < 1e-4) d.bend = 0.0;
        if (!d.track || (!d.playing && !d.scratch)) {
            d.level *= 0.8;
            continue;
        }
        const DjTrack& t = *d.track;
        // Stem-Lautstärken weich nachführen (kein Knacken)
        const std::array<float, 4> from = d.stemGain;
        std::array<float, 4> to{};
        for (int s = 0; s < 4; ++s) to[static_cast<std::size_t>(s)] = d.stemOn[static_cast<std::size_t>(s)] ? d.stemLevel[static_cast<std::size_t>(s)] : 0.0f;
        const bool ramp = from != to;

        bool useStretch = false;
#ifdef LOGGX_WITH_RUBBERBAND
        useStretch = !d.scratch && d.brakeStep == 0.0 && ((d.keyLock && std::abs(d.baseRate() - 1.0) > 1e-4) || d.keyShift != 0);
        // Tonhöhe: mit Key Lock nur die Verschiebung, sonst wie Vinyl (Tempo) plus Verschiebung
        const double pitch = std::pow(2.0, d.keyShift / 12.0) * (d.keyLock ? 1.0 : d.rate());
        if (useStretch) {
            using RB = RubberBand::RubberBandStretcher;
            if (!d.stretcher)
                d.stretcher = std::make_unique<RB>(static_cast<std::size_t>(m_rate), 2,
                                                   RB::OptionProcessRealTime | RB::OptionEngineFaster | RB::OptionChannelsTogether,
                                                   1.0 / d.rate(), 1.0);
            if (d.stretchReset) {
                d.stretcher->reset();
                d.stretchReset = false;
            }
            d.stretcher->setTimeRatio(1.0 / d.rate());
            d.stretcher->setPitchScale(pitch);
            d.outL.resize(frames);
            d.outR.resize(frames);
            std::size_t done = 0;
            int guard = 0;
            while (done < frames && guard++ < 64) {
                const int avail = d.stretcher->available();
                if (avail > 0) {
                    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(avail), frames - done);
                    float* outs[2] = {d.outL.data() + done, d.outR.data() + done};
                    d.stretcher->retrieve(outs, take);
                    done += take;
                    continue;
                }
                std::size_t need = d.stretcher->getSamplesRequired();
                if (need == 0) need = 512;
                d.inL.resize(need);
                d.inR.resize(need);
                for (std::size_t k = 0; k < need; ++k) {
                    const std::size_t idx = d.wrap(std::floor(d.pos));
                    d.sampleAt(idx, d.inL[k], d.inR[k]);
                    d.pos = static_cast<double>(idx) + 1.0;
                    if (d.loop && d.pos >= d.loopEnd) d.pos = d.loopStart;
                }
                const float* ins[2] = {d.inL.data(), d.inR.data()};
                d.stretcher->process(ins, need, false);
            }
            for (std::size_t k = 0; k < frames; ++k) {
                buf[k * 2] = k < done ? d.outL[k] : 0.0f;
                buf[k * 2 + 1] = k < done ? d.outR[k] : 0.0f;
            }
        }
#endif
        if (!useStretch) {
            d.stretchReset = true;
            // Scratchen: Geschwindigkeit folgt dem Jog-Rad (weich geglättet)
            double step = d.rate();
            if (d.scratch) {
                const double target = d.scratchPending / static_cast<double>(frames);
                d.scratchVel = 0.55 * d.scratchVel + 0.45 * std::clamp(target, -4.0, 4.0);
                d.scratchPending -= d.scratchVel * static_cast<double>(frames);
                if (std::abs(d.scratchPending) < 1.0 && std::abs(d.scratchVel) < 0.02) d.scratchVel *= 0.5;
                step = d.scratchVel;
            }
            for (std::size_t k = 0; k < frames; ++k) {
                if (ramp) {
                    const float a = static_cast<float>(k) / static_cast<float>(frames);
                    for (int s = 0; s < 4; ++s)
                        d.stemGain[static_cast<std::size_t>(s)] =
                            from[static_cast<std::size_t>(s)] + (to[static_cast<std::size_t>(s)] - from[static_cast<std::size_t>(s)]) * a;
                }
                // Kubische Interpolation (Catmull-Rom) zwischen den Samples
                const double p = d.pos;
                const double base = std::floor(p);
                const float f = static_cast<float>(p - base);
                float l[4], r[4];
                for (int j = 0; j < 4; ++j) d.sampleAt(d.wrap(base + j - 1), l[j], r[j]);
                auto cr = [f](const float* y) {
                    return y[1] + 0.5f * f * (y[2] - y[0] + f * (2.0f * y[0] - 5.0f * y[1] + 4.0f * y[2] - y[3] + f * (3.0f * (y[1] - y[2]) + y[3] - y[0])));
                };
                buf[k * 2] = cr(l);
                buf[k * 2 + 1] = cr(r);
                d.pos = std::max(0.0, d.pos + step);
                if (d.loop && d.loopEnd > d.loopStart && d.pos >= d.loopEnd) d.pos -= d.loopEnd - d.loopStart;
                if (d.brakeStep != 0.0) {
                    // Bremse: Faktor 1 → 0; Backspin: −3 → 0 (rückwärts auslaufen)
                    d.brake -= d.brakeStep;
                    if ((d.brakeStep > 0.0 && d.brake <= 0.0) || (d.brakeStep < 0.0 && d.brake >= 0.0)) {
                        d.playing = false;
                        d.brake = 1.0;
                        d.brakeStep = 0.0;
                        m_fx->on = false;
                        break;
                    }
                }
            }
        }
        d.stemGain = to;
        if (d.pos >= t.frames()) {
            d.playing = false;
            d.pos = static_cast<double>(t.frames());
        }
        // Kanalzug: Trim → EQ → Filter → (Pegel) → Fader – alles weich nachgeführt
        {
            const double a = 1.0 - std::exp(-static_cast<double>(frames) / (m_rate * 0.025));   // ~25 ms
            for (int b = 0; b < 3; ++b) {
                d.eqS[b] += (d.eq[b] - d.eqS[b]) * a;
                if (std::abs(d.eq[b] - d.eqS[b]) < 1e-3) d.eqS[b] = d.eq[b];
            }
            d.filtS += (d.filt - d.filtS) * a;
            if (std::abs(d.filt - d.filtS) < 1e-3) d.filtS = d.filt;
        }
        d.designFilters(m_rate);
        const bool eqOn = d.eqS[0] != 0 || d.eqS[1] != 0 || d.eqS[2] != 0;
        const bool filtOn = std::abs(d.filtS) > 0.02;
        const float trim0 = static_cast<float>(d.trimS), trim1 = static_cast<float>(d.trim);
        const float fade0 = static_cast<float>(d.fadeS), fade1 = static_cast<float>(d.fader * d.fader);   // Fader-Kurve
        const float inv = 1.0f / static_cast<float>(std::max<std::size_t>(1, frames));
        double peak = 0;
        for (std::size_t k = 0; k < frames; ++k) {
            const float r = static_cast<float>(k) * inv;
            const float trim = trim0 + (trim1 - trim0) * r, fade = fade0 + (fade1 - fade0) * r;
            for (int c = 0; c < 2; ++c) {
                float x = buf[k * 2 + c] * trim;
                if (eqOn) x = d.high.process(d.mid.process(d.low.process(x, c), c), c);
                if (filtOn) x = d.filter.process(x, c);
                peak = std::max(peak, static_cast<double>(std::abs(x)));
                if (cue && d.pfl) m_cueBus[k * 2 + c] += x;   // vor dem Fader
                buf[k * 2 + c] = x * fade;
            }
        }
        d.trimS = d.trim;
        d.fadeS = d.fader * d.fader;
        d.level = std::max(peak, d.level * 0.85);
    }

    // Effekt auf ein Deck oder den Master
    auto applyFx = [&](float* buf) {
        Fx& fx = *m_fx;
        // Roll braucht immer die letzten Sekunden des Ziels
        {
            const std::size_t len = fx.roll.size() / 2;
            for (std::size_t k = 0; k < frames; ++k) {
                fx.roll[fx.rollPos * 2] = buf[k * 2];
                fx.roll[fx.rollPos * 2 + 1] = buf[k * 2 + 1];
                fx.rollPos = (fx.rollPos + 1) % len;
            }
            if (!fx.on || fx.type != DjFx::Roll) fx.rollActive = false;
        }
        if (!fx.on || fx.type == DjFx::Brake || fx.type == DjFx::Backspin) return;
        const Deck& ref = *m_decks[static_cast<std::size_t>(fx.target == 2 ? m_master : fx.target)];
        const double bpm = ref.bpm() > 0 ? ref.bpm() : 120.0;
        const float wet = static_cast<float>(fx.wet);
        // LFO im Takt: eine Periode = fx.beats Schläge
        const double lfoStep = bpm / 60.0 / std::max(0.0625, fx.beats) / m_rate;
        if (fx.type == DjFx::Echo || fx.type == DjFx::PingPong) {
            const std::size_t len = fx.delay.size() / 2;
            const std::size_t dl = std::clamp<std::size_t>(static_cast<std::size_t>(fx.beats * 60.0 / bpm * m_rate), 1, len - 1);
            const bool pp = fx.type == DjFx::PingPong;
            for (std::size_t k = 0; k < frames; ++k) {
                const std::size_t rd = (fx.delayPos + len - dl) % len;
                const float dL = fx.delay[rd * 2], dR = fx.delay[rd * 2 + 1];
                if (pp) {   // Ping-Pong: Echo springt zwischen links und rechts
                    const float in = 0.5f * (buf[k * 2] + buf[k * 2 + 1]);
                    fx.delay[fx.delayPos * 2] = in + dR * 0.6f;
                    fx.delay[fx.delayPos * 2 + 1] = dL * 0.6f;
                } else {
                    fx.delay[fx.delayPos * 2] = buf[k * 2] + dL * 0.55f;
                    fx.delay[fx.delayPos * 2 + 1] = buf[k * 2 + 1] + dR * 0.55f;
                }
                buf[k * 2] += dL * wet;
                buf[k * 2 + 1] += dR * wet;
                fx.delayPos = (fx.delayPos + 1) % len;
            }
        } else if (fx.type == DjFx::Flanger) {
            const std::size_t len = fx.flange.size() / 2;
            for (std::size_t k = 0; k < frames; ++k) {
                fx.lfo = std::fmod(fx.lfo + lfoStep, 1.0);
                const double ms = 1.0 + 4.0 * (0.5 - 0.5 * std::cos(2 * kPi * fx.lfo));
                const double d = ms * 0.001 * m_rate;
                const double rp = static_cast<double>(fx.flangePos) + len - d;
                const std::size_t i0 = static_cast<std::size_t>(rp) % len, i1 = (i0 + 1) % len;
                const float t = static_cast<float>(rp - std::floor(rp));
                for (int c = 0; c < 2; ++c) {
                    const float delayed = fx.flange[i0 * 2 + c] * (1 - t) + fx.flange[i1 * 2 + c] * t;
                    fx.flange[fx.flangePos * 2 + c] = buf[k * 2 + c] + delayed * 0.6f;
                    buf[k * 2 + c] = buf[k * 2 + c] * (1 - 0.5f * wet) + delayed * wet * 0.7f;
                }
                fx.flangePos = (fx.flangePos + 1) % len;
            }
        } else if (fx.type == DjFx::Phaser) {
            for (std::size_t k = 0; k < frames; ++k) {
                fx.lfo = std::fmod(fx.lfo + lfoStep, 1.0);
                const double hz = 300.0 * std::pow(10.0, 1.0 * (0.5 - 0.5 * std::cos(2 * kPi * fx.lfo)));
                const float a = static_cast<float>((std::tan(kPi * hz / m_rate) - 1) / (std::tan(kPi * hz / m_rate) + 1));
                for (int c = 0; c < 2; ++c) {
                    float x = buf[k * 2 + c] + fx.phaserFb[c] * 0.5f;
                    for (float& z : fx.ap[static_cast<std::size_t>(c)]) {
                        const float y = a * x + z;
                        z = x - a * y;
                        x = y;
                    }
                    fx.phaserFb[c] = x;
                    buf[k * 2 + c] = buf[k * 2 + c] * (1 - 0.5f * wet) + x * wet * 0.5f + buf[k * 2 + c] * 0.5f * wet;
                }
            }
        } else if (fx.type == DjFx::Trans) {
            // Zerhacker im Takt, am Raster des Ziel-Decks ausgerichtet
            double phase = fx.lfo;
            if (ref.track && ref.gridBpm > 0) {
                const double t = ref.pos / ref.track->sampleRate;
                phase = std::fmod((t - ref.gridFirst) / (60.0 / ref.gridBpm) / std::max(0.0625, fx.beats) + 1000.0, 1.0);
            }
            for (std::size_t k = 0; k < frames; ++k) {
                phase = std::fmod(phase + lfoStep, 1.0);
                // offen in der ersten Hälfte, sanfte Kanten (~3 ms)
                const double edge = 0.003 * bpm / 60.0 / std::max(0.0625, fx.beats);
                double g = phase < 0.5 ? 1.0 : 0.0;
                if (phase < edge) g = phase / edge;
                else if (phase > 0.5 && phase < 0.5 + edge) g = 1.0 - (phase - 0.5) / edge;
                const float gain = static_cast<float>(1.0 - wet * (1.0 - g));
                buf[k * 2] *= gain;
                buf[k * 2 + 1] *= gain;
            }
            fx.lfo = phase;
        } else if (fx.type == DjFx::Roll) {
            // Wiederholt das Stück direkt vor dem Einschalten (Länge = Schläge)
            const std::size_t len = fx.roll.size() / 2;
            const std::size_t L = std::clamp<std::size_t>(static_cast<std::size_t>(fx.beats * 60.0 / bpm * m_rate), 64, len - frames - 1);
            if (!fx.rollActive) {
                fx.rollActive = true;
                fx.rollLen = L;
                fx.rollSeg.resize(L * 2);
                // Ring wurde schon um diesen Block weitergeschrieben: davor beginnen
                const std::size_t start = (fx.rollPos + 2 * len - frames - L) % len;
                for (std::size_t i = 0; i < L; ++i) {
                    fx.rollSeg[i * 2] = fx.roll[((start + i) % len) * 2];
                    fx.rollSeg[i * 2 + 1] = fx.roll[((start + i) % len) * 2 + 1];
                }
                fx.rollRead = 0;
            }
            for (std::size_t k = 0; k < frames; ++k) {
                for (int c = 0; c < 2; ++c) buf[k * 2 + c] = buf[k * 2 + c] * (1 - wet) + fx.rollSeg[fx.rollRead * 2 + c] * wet;
                fx.rollRead = (fx.rollRead + 1) % fx.rollLen;
            }
        } else if (fx.type == DjFx::Filter) {
            for (std::size_t k = 0; k < frames; k += 32) {
                fx.lfo = std::fmod(fx.lfo + lfoStep * 32, 1.0);
                const double hz = 250.0 * std::pow(40.0, 0.5 - 0.5 * std::cos(2 * kPi * fx.lfo));
                fx.sweep.design(Biquad::Type::LowPass, hz, 0, 2.5, m_rate);
                for (std::size_t j = k; j < std::min(frames, k + 32); ++j)
                    for (int c = 0; c < 2; ++c) {
                        const float f = fx.sweep.process(buf[j * 2 + c], c);
                        buf[j * 2 + c] = buf[j * 2 + c] * (1 - wet) + f * wet;
                    }
            }
        } else if (fx.type == DjFx::Noise) {
            for (std::size_t k = 0; k < frames; ++k) {
                for (int c = 0; c < 2; ++c) {
                    fx.noiseSeed = fx.noiseSeed * 1664525u + 1013904223u;
                    const float n = static_cast<float>(fx.noiseSeed >> 8) / 8388608.0f - 1.0f;
                    const float hp = n - fx.noiseLp[c];
                    fx.noiseLp[c] += 0.2f * (n - fx.noiseLp[c]);
                    buf[k * 2 + c] += hp * 0.18f * wet;
                }
            }
        } else if (fx.type == DjFx::Crush) {
            // Bitcrusher: weniger Bits und Abtastrate halten (stärker mit „wet“)
            const float levels = static_cast<float>(std::pow(2.0, 12.0 - 9.0 * fx.wet));
            const int hold = 1 + static_cast<int>(fx.wet * 10.0);
            for (std::size_t k = 0; k < frames; ++k) {
                if (fx.crushCount++ % hold == 0) {
                    fx.crushHold[0] = std::round(buf[k * 2] * levels) / levels;
                    fx.crushHold[1] = std::round(buf[k * 2 + 1] * levels) / levels;
                }
                buf[k * 2] = buf[k * 2] * (1 - wet) + fx.crushHold[0] * wet;
                buf[k * 2 + 1] = buf[k * 2 + 1] * (1 - wet) + fx.crushHold[1] * wet;
            }
        } else if (fx.type == DjFx::Reverb) {
            fx.mono.resize(frames);
            fx.wetL.resize(frames);
            fx.wetR.resize(frames);
            for (std::size_t k = 0; k < frames; ++k) fx.mono[k] = 0.5f * (buf[k * 2] + buf[k * 2 + 1]);
            fx.reverb.process(fx.mono.data(), fx.wetL.data(), fx.wetR.data(), frames);
            for (std::size_t k = 0; k < frames; ++k) {
                buf[k * 2] += fx.wetL[k] * wet * 2.0f;
                buf[k * 2 + 1] += fx.wetR[k] * wet * 2.0f;
            }
        }
    };
    if (m_fx->target == 0) applyFx(m_bufA.data());
    if (m_fx->target == 1) applyFx(m_bufB.data());

    // Crossfader mit wählbarer Kurve, Pegel über den Block weich geführt
    const double x = (m_cross + 1.0) * 0.5;
    float ga, gb;
    if (m_xfCurve == 2) {   // Cut: fast sofort voll, nur ganz außen weg
        ga = static_cast<float>(std::clamp((1.0 - x) / 0.06, 0.0, 1.0));
        gb = static_cast<float>(std::clamp(x / 0.06, 0.0, 1.0));
    } else {
        ga = static_cast<float>(std::cos(x * kPi / 2));
        gb = static_cast<float>(std::sin(x * kPi / 2));
        if (m_xfCurve == 1) {   // Mix: Mitte nicht leiser als außen
            const float norm = 1.0f / std::max(ga, gb);
            ga *= norm;
            gb *= norm;
        }
    }
    if (m_crossGa < 0) m_crossGa = ga, m_crossGb = gb;
    {
        const float a0 = static_cast<float>(m_crossGa), b0 = static_cast<float>(m_crossGb);
        const float inv = 1.0f / static_cast<float>(std::max<std::size_t>(1, frames));
        for (std::size_t k = 0; k < frames; ++k) {
            const float r = static_cast<float>(k) * inv;
            const float A = a0 + (ga - a0) * r, B = b0 + (gb - b0) * r;
            out[k * 2] = m_bufA[k * 2] * A + m_bufB[k * 2] * B;
            out[k * 2 + 1] = m_bufA[k * 2 + 1] * A + m_bufB[k * 2 + 1] * B;
        }
        m_crossGa = ga;
        m_crossGb = gb;
    }
    if (m_fx->target == 2) applyFx(out);
    // Sampler (One-Shots) direkt in den Master
    const float sv = static_cast<float>(m_samplerVol);
    for (Voice& v : m_samples) {
        if (!v.playing || !v.data) continue;
        const std::vector<float>& d = *v.data;
        for (std::size_t k = 0; k < frames && v.pos + 1 < d.size(); ++k, v.pos += 2) {
            out[k * 2] += d[v.pos] * sv;
            out[k * 2 + 1] += d[v.pos + 1] * sv;
        }
        if (v.pos + 1 >= d.size()) v.playing = false;
    }
    double peak = 0;
    const float vol = static_cast<float>(m_masterVol);
    for (std::size_t i = 0; i < frames * 2; ++i) {
        float v = out[i] * vol;
        // Weicher Begrenzer gegen Übersteuern
        if (v > 0.9f) v = 0.9f + 0.1f * std::tanh((v - 0.9f) / 0.1f);
        if (v < -0.9f) v = -0.9f + 0.1f * std::tanh((v + 0.9f) / 0.1f);
        out[i] = v;
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    }
    m_masterLevel = std::max(peak, m_masterLevel * 0.85);
    if (cue) {
        // Kopfhörer: Vorhören ↔ Master überblenden
        const float mixAmt = static_cast<float>(m_headMix), hv = static_cast<float>(m_headVol);
        for (std::size_t i = 0; i < frames * 2; ++i) {
            float v = (m_cueBus[i] * (1.0f - mixAmt) + out[i] * mixAmt) * hv;
            v = std::clamp(v, -1.0f, 1.0f);
            cue[i] = v;
        }
    }
    if (m_recorder) m_recorder(out, frames);
}

}  // namespace loggx
