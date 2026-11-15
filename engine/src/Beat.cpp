// Tempo-Erkennung: Einsatz-Hüllkurve (Anstieg der Energie) und deren
// Autokorrelation. Gewichtung um 120 BPM gegen Oktavfehler (halbes/doppeltes
// Tempo), danach Suche nach der Phase des ersten Schlags.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <complex>
#include <vector>

#include "../../third_party/pocketfft/pocketfft_hdronly.h"

#include "loggx/engine/Engine.h"

namespace loggx {

namespace {

// Einsatz-Kurve per spektralem Fluss (STFT, logarithmisch), zusätzlich mit
// eigenem Gewicht für die Tiefen (Bassdrum). hopSec = Abstand der Werte.
void onsetCurve(const float* mono, std::size_t count, int rate, std::vector<double>& onset, std::vector<double>& low,
                double& hopSec, double& latency) {
    const int n = rate > 30000 ? 2048 : 1024;
    const int hop = std::max(64, static_cast<int>(std::lround(rate * 0.0058)));   // ≈ 5,8 ms
    hopSec = static_cast<double>(hop) / rate;
    latency = 0.83 * n / rate;   // gemessen: stärkster Anstieg kurz vor Fensterende
    const std::size_t frames = count > static_cast<std::size_t>(n) ? (count - n) / hop + 1 : 0;
    onset.assign(frames, 0.0);
    low.assign(frames, 0.0);
    if (frames < 4) return;
    const int bins = n / 2 + 1;
    const int lowBins = std::max(2, static_cast<int>(150.0 * n / rate));
    std::vector<float> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / n));
    std::vector<float> prev(static_cast<std::size_t>(bins), 0.0f);
    std::vector<float> frame(static_cast<std::size_t>(n));
    std::vector<std::complex<float>> spec(static_cast<std::size_t>(bins));
    std::vector<float> mag(static_cast<std::size_t>(bins));
    const pocketfft::shape_t shape = {static_cast<std::size_t>(n)};
    const pocketfft::stride_t rs = {sizeof(float)}, cs = {sizeof(std::complex<float>)};
    for (std::size_t f = 0; f < frames; ++f) {
        const float* src = mono + f * hop;
        for (int i = 0; i < n; ++i) frame[static_cast<std::size_t>(i)] = src[i] * window[static_cast<std::size_t>(i)];
        pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, frame.data(), spec.data(), 1.0f);
        double flux = 0, lowFlux = 0;
        for (int k = 1; k < bins; ++k) {
            const float m = std::log1p(100.0f * std::abs(spec[static_cast<std::size_t>(k)]));
            const float d = m - prev[static_cast<std::size_t>(k)];
            prev[static_cast<std::size_t>(k)] = m;
            if (d > 0) {
                flux += d;
                if (k <= lowBins) lowFlux += d;
            }
        }
        onset[f] = flux;
        low[f] = lowFlux;
    }
    // Gleitenden Mittelwert (≈ 0,4 s) abziehen, nur positive Spitzen behalten
    auto clean = [&](std::vector<double>& v) {
        const int win = std::max(3, static_cast<int>(0.4 / hopSec));
        std::vector<double> out(v.size(), 0.0);
        double sum = 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            sum += v[i];
            if (i >= static_cast<std::size_t>(win)) sum -= v[i - win];
            const double mean = sum / static_cast<double>(std::min<std::size_t>(i + 1, static_cast<std::size_t>(win)));
            out[i] = std::max(0.0, v[i] - mean);
        }
        // auf mittlere Stärke 1 normieren
        double total = 0;
        for (double x : out) total += x;
        const double scale = total > 0 ? static_cast<double>(out.size()) / total : 1.0;
        for (double& x : out) x *= scale;
        v.swap(out);
    };
    clean(onset);
    clean(low);
}

// Mittlere Einsatz-Stärke auf einem Raster (Periode/Phase in Abschnitten)
double gridScore(const std::vector<double>& o, double period, double phase) {
    double total = 0;
    int n = 0;
    for (double x = phase; x < static_cast<double>(o.size()) - 1; x += period, ++n) {
        const std::size_t i = static_cast<std::size_t>(x);
        const double t = x - static_cast<double>(i);
        // kleines Fenster: Einsätze liegen selten exakt auf dem Wert
        double v = o[i] * (1 - t) + o[i + 1] * t;
        if (i > 0) v = std::max(v, 0.6 * o[i - 1]);
        if (i + 2 < o.size()) v = std::max(v, 0.6 * o[i + 2]);
        total += v;
    }
    return n > 0 ? total / n : 0.0;
}

}  // namespace

BeatInfo detectBeatsFromSamples(const float* mono, std::size_t count, int rate, double minBpm, double maxBpm) {
    BeatInfo info;
    std::vector<double> full, low;
    double hopSec = 0, latency = 0;
    onsetCurve(mono, count, rate, full, low, hopSec, latency);
    const std::size_t frames = full.size();
    if (frames < 256) return info;
    minBpm = std::clamp(minBpm, 40.0, 200.0);
    maxBpm = std::clamp(maxBpm, minBpm * 1.9, 260.0);
    std::vector<double> onset(frames);
    for (std::size_t i = 0; i < frames; ++i) onset[i] = full[i] + 0.8 * low[i];

    // 1) Grob: Autokorrelation 50–220 BPM getrennt für Tiefen (Bassdrum) und
    //    alles; gestützt durch doppelte und vierfache Periode (Takt).
    const int lo = std::max(2, static_cast<int>(std::floor(60.0 / 220.0 / hopSec)));
    const int hi = std::min(static_cast<int>(frames / 5), static_cast<int>(std::ceil(60.0 / 50.0 / hopSec)));
    if (hi <= lo + 4) return info;
    const int maxLag = std::min(static_cast<int>(frames) - 1, hi * 4 + 2);
    auto autocorr = [&](const std::vector<double>& v) {
        std::vector<double> ac(static_cast<std::size_t>(maxLag) + 1, 0.0);
        for (int lag = lo; lag <= maxLag; ++lag) {
            double a = 0.0;
            for (std::size_t i = 0; i + lag < frames; ++i) a += v[i] * v[i + lag];
            ac[static_cast<std::size_t>(lag)] = a / static_cast<double>(frames - lag);
        }
        double mean = 0;
        for (int lag = lo; lag <= hi; ++lag) mean += ac[static_cast<std::size_t>(lag)];
        mean = std::max(1e-12, mean / (hi - lo + 1));
        for (double& x : ac) x /= mean;
        return ac;
    };
    const std::vector<double> acL = autocorr(low), acF = autocorr(full);
    auto acAt = [&](const std::vector<double>& ac, int lag) { return lag <= maxLag ? ac[static_cast<std::size_t>(lag)] : 0.0; };
    const double center = std::sqrt(minBpm * maxBpm);
    int best = -1;
    double bestScore = -1;
    for (int lag = lo; lag <= hi; ++lag) {
        const double bpm = 60.0 / (lag * hopSec);
        const double w = std::exp(-0.5 * std::pow(std::log2(bpm / center) / 1.0, 2.0));
        const double sl = acAt(acL, lag) + 0.5 * acAt(acL, 2 * lag) + 0.25 * acAt(acL, 4 * lag);
        const double sf = acAt(acF, lag) + 0.5 * acAt(acF, 2 * lag) + 0.25 * acAt(acF, 4 * lag);
        const double score = (sl + 0.7 * sf) * w;
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best < 0) return info;
    double bpm = 60.0 / (best * hopSec);
    // In den gewünschten Bereich falten (halbes/doppeltes Tempo)
    while (bpm < minBpm) bpm *= 2;
    while (bpm > maxBpm) bpm /= 2;

    // 2) Fein: Raster über die ganze Länge, ±3 % in 0,01-BPM-Schritten;
    //    über viele Schläge summiert ist das sehr genau.
    double bestBpm = bpm, bestPhase = 0, bestFine = -1;
    auto evaluate = [&](double candidate) {
        const double period = 60.0 / candidate / hopSec;
        const int phases = std::max(16, static_cast<int>(period));
        double top = -1, topPhase = 0;
        for (int p = 0; p < phases; ++p) {
            const double phase = period * p / phases;
            const double s = gridScore(onset, period, phase);
            if (s > top) {
                top = s;
                topPhase = phase;
            }
        }
        if (top > bestFine) {
            bestFine = top;
            bestBpm = candidate;
            bestPhase = topPhase;
        }
    };
    // Kandidaten jeweils fein über die ganze Länge prüfen.
    auto fineSearch = [&](double cand) {
        bestFine = -1;
        for (double c = cand * 0.985; c <= cand * 1.015; c += 0.05) evaluate(c);
        const double coarse = bestBpm;
        for (double c = coarse - 0.06; c <= coarse + 0.06; c += 0.01) evaluate(c);
    };
    // a) 3:2-Verwechslungen: das richtige Raster trifft im Mittel die
    //    stärksten Einsätze (ein ⅔-Raster landet zur Hälfte auf Offbeats).
    {
        double top = -1, topBpm = bpm;
        for (double factor : {1.0, 1.5, 2.0 / 3.0}) {
            double cand = bpm * factor;
            while (cand < minBpm) cand *= 2;
            while (cand > maxBpm) cand /= 2;
            fineSearch(cand);
            if (bestFine > top * (factor == 1.0 ? 1.0 : 1.03)) {
                top = bestFine;
                topBpm = bestBpm;
            }
        }
        bpm = topBpm;
    }
    // b) Oktave: halbes und doppeltes Tempo (im Bereich)
    struct Cand { double bpm, phase, on, mid; };
    std::vector<Cand> cands;
    for (double factor : {0.5, 1.0, 2.0}) {
        const double cand = bpm * factor;
        if (cand < minBpm - 0.5 || cand > maxBpm + 0.5) continue;
        fineSearch(cand);
        const double per = 60.0 / bestBpm / hopSec;
        cands.push_back({bestBpm, bestPhase, bestFine, gridScore(onset, per, bestPhase + per / 2)});
    }
    // Vom langsamsten Kandidaten aus: liegen genau zwischen den Schlägen
    // ebenfalls kräftige Einsätze, ist das doppelte Tempo gemeint.
    std::size_t pick = 0;
    // Unter ~95 BPM reicht schon weniger (DnB/Dubstep werden sonst halbiert).
    while (pick + 1 < cands.size() && cands[pick].mid > (cands[pick].bpm < 95 ? 0.18 : 0.4) * cands[pick].on) ++pick;
    double winner = cands.empty() ? bpm : cands[pick].bpm;
    double winnerPhase = cands.empty() ? 0.0 : cands[pick].phase;
    double winnerFine = cands.empty() ? 1.0 : cands[pick].on;
    bpm = winner;
    bestBpm = winner;
    bestPhase = winnerPhase;
    bestFine = winnerFine;
    // Ganzzahlig, wenn sehr nah (die meiste Musik hat ein ganzzahliges Tempo)
    if (std::abs(bpm - std::round(bpm)) < 0.12) {
        const double before = bestFine;
        const double keepPhase = bestPhase;
        bestFine = -1;
        evaluate(std::round(bpm));
        if (bestFine < before * 0.97) {   // gerundet deutlich schlechter: lassen
            bestBpm = bpm;
            bestPhase = keepPhase;
            bestFine = before;
        }
        bpm = bestBpm;
    }
    info.bpm = std::round(bpm * 100.0) / 100.0;

    // 3) Taktanfang: von den vier möglichen Schlägen den mit der stärksten Bassdrum
    const double period = 60.0 / info.bpm / hopSec;
    // Schlag oder Offbeat? Die Bassdrum liegt auf dem Schlag.
    if (gridScore(low, period, bestPhase + period / 2) > 1.05 * gridScore(low, period, bestPhase))
        bestPhase = std::fmod(bestPhase + period / 2, period);
    double barPhase = bestPhase, barBest = -1;
    for (int k = 0; k < 4; ++k) {
        const double ph = bestPhase + k * period;
        const double s = gridScore(low, period * 4, ph);
        if (s > barBest) {
            barBest = s;
            barPhase = ph;
        }
    }
    double first = std::fmod(barPhase * hopSec + latency, period * 4 * hopSec);
    info.firstBeat = static_cast<TimeUs>(first * kSecond);
    // Sicherheit: Rasterstärke gegenüber dem Durchschnitt der Einsätze (=1)
    double mean = 0;
    for (double x : onset) mean += x;
    mean = std::max(1e-9, mean / static_cast<double>(frames));
    info.confidence = std::clamp((bestFine / mean - 2.0) / 30.0, 0.0, 1.0);
    return info;
}

BeatInfo detectBeats(const std::string& path, TimeUs maxDuration, double minBpm, double maxBpm) {
    constexpr int rate = 22050;
    AudioReader reader;
    if (!reader.open(path, rate)) return {};
    const std::size_t maxFrames = static_cast<std::size_t>(maxDuration / kSecond * rate);
    std::vector<float> stereo(8192 * 2), mono;
    mono.reserve(maxFrames);
    std::size_t got;
    while (mono.size() < maxFrames && (got = reader.read(stereo.data(), 8192)) > 0)
        for (std::size_t i = 0; i < got; ++i) mono.push_back(0.5f * (stereo[i * 2] + stereo[i * 2 + 1]));
    return detectBeatsFromSamples(mono.data(), mono.size(), rate, minBpm, maxBpm);
}

// ---------------------------------------------------------------- Live

void LiveSynth::noteOn(int pitch, double velocity, InstrumentType type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Gleiche Taste nochmal: alten Ton loslassen
    for (auto& v : m_voices)
        if (v.pitch == pitch && v.held < 0) v.held = std::max<std::int64_t>(1, v.pos);
    m_voices.push_back({pitch, velocity, type});
    if (m_voices.size() > 64) m_voices.erase(m_voices.begin());
}

void LiveSynth::noteOff(int pitch) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& v : m_voices)
        if (v.pitch == pitch && v.held < 0) v.held = std::max<std::int64_t>(1, v.pos);
}

void LiveSynth::allNotesOff() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& v : m_voices)
        if (v.held < 0) v.held = std::max<std::int64_t>(1, v.pos);
}

int LiveSynth::activeVoices() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_voices.size());
}

void LiveSynth::render(float* out, std::size_t frames) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mono.assign(frames, 0.0f);
    for (auto& v : m_voices) {
        // Solange gedrückt: Haltedauer liegt immer hinter dem Block.
        const std::int64_t held = v.held < 0 ? v.pos + static_cast<std::int64_t>(frames) + 1 : v.held;
        synth::renderNote(v.type, v.pitch, v.velocity, held, v.pos, m_mono.data(), frames, m_rate);
        v.pos += static_cast<std::int64_t>(frames);
    }
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Voice& v) {
                       const std::int64_t tail = static_cast<std::int64_t>(synth::releaseTime(v.type) * m_rate);
                       return (v.held >= 0 && v.pos > v.held + tail) || v.pos > 30LL * m_rate;
                   }),
                   m_voices.end());
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = std::clamp(m_mono[i], -1.0f, 1.0f);
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
}

}  // namespace loggx
