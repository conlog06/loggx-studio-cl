// Tempo-Erkennung: Einsatz-Hüllkurve (Anstieg der Energie) und deren
// Autokorrelation. Gewichtung um 120 BPM gegen Oktavfehler (halbes/doppeltes
// Tempo), danach Suche nach der Phase des ersten Schlags.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "loggx/engine/Engine.h"

namespace loggx {

BeatInfo detectBeatsFromSamples(const float* mono, std::size_t count, int rate) {
    BeatInfo info;
    constexpr int hop = 256;
    const std::size_t frames = count / hop;
    if (frames < 64) return info;

    // Energie pro Abschnitt, logarithmisch; Einsatz = positiver Anstieg.
    std::vector<double> energy(frames), onset(frames, 0.0);
    for (std::size_t i = 0; i < frames; ++i) {
        double e = 0.0;
        for (int k = 0; k < hop; ++k) {
            const double v = mono[i * hop + k];
            e += v * v;
        }
        energy[i] = std::log(1e-9 + e);
    }
    for (std::size_t i = 1; i < frames; ++i) onset[i] = std::max(0.0, energy[i] - energy[i - 1]);
    // Gleitenden Mittelwert abziehen (≈ 0,5 s), damit nur echte Spitzen zählen.
    const double hopSec = static_cast<double>(hop) / rate;
    const int win = std::max(3, static_cast<int>(0.5 / hopSec));
    std::vector<double> clean(frames, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        sum += onset[i];
        if (i >= static_cast<std::size_t>(win)) sum -= onset[i - win];
        const double mean = sum / std::min<std::size_t>(i + 1, static_cast<std::size_t>(win));
        clean[i] = std::max(0.0, onset[i] - mean);
    }

    // Autokorrelation für 60..200 BPM
    const double minLag = 60.0 / 200.0 / hopSec, maxLag = 60.0 / 60.0 / hopSec;
    const int lo = std::max(1, static_cast<int>(std::floor(minLag)));
    const int hi = std::min(static_cast<int>(frames / 2), static_cast<int>(std::ceil(maxLag)));
    if (hi <= lo + 2) return info;
    std::vector<double> ac(static_cast<std::size_t>(hi) + 2, 0.0);
    for (int lag = lo; lag <= hi + 1 && lag < static_cast<int>(frames); ++lag) {
        double a = 0.0;
        for (std::size_t i = 0; i + lag < frames; ++i) a += clean[i] * clean[i + lag];
        ac[static_cast<std::size_t>(lag)] = a / static_cast<double>(frames - lag);
    }
    int best = -1;
    double bestScore = 0.0;
    for (int lag = lo; lag <= hi; ++lag) {
        const double bpm = 60.0 / (lag * hopSec);
        // log-Gauß um 120 BPM, Breite ca. eine Oktave
        const double w = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 0.9, 2.0));
        // Auch die doppelte Periode stützt das Tempo (Takt-Struktur).
        const double support = 2 * lag <= hi + 1 ? 0.5 * ac[static_cast<std::size_t>(std::min(2 * lag, hi + 1))] : 0.0;
        const double score = (ac[static_cast<std::size_t>(lag)] + support) * w;
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best < 0 || bestScore <= 0.0) return info;
    // Parabel durch die Nachbarn für eine Periode zwischen den Rasterpunkten.
    double period = best;
    if (best > lo && best < hi) {
        const double y0 = ac[static_cast<std::size_t>(best - 1)], y1 = ac[static_cast<std::size_t>(best)],
                     y2 = ac[static_cast<std::size_t>(best + 1)];
        const double denom = y0 - 2 * y1 + y2;
        if (std::abs(denom) > 1e-12) period += std::clamp(0.5 * (y0 - y2) / denom, -0.5, 0.5);
    }
    info.bpm = 60.0 / (period * hopSec);
    // Auf typische Werte runden, wenn sehr nah (Musik ist meist ganzzahlig).
    if (std::abs(info.bpm - std::round(info.bpm)) < 0.15) info.bpm = std::round(info.bpm);
    period = 60.0 / info.bpm / hopSec;

    // Phase: Versatz mit der größten Einsatz-Summe auf dem Raster.
    const int steps = std::max(8, static_cast<int>(period * 4));
    double bestPhase = 0.0, bestSum = -1.0;
    for (int s = 0; s < steps; ++s) {
        const double phase = period * s / steps;
        double total = 0.0;
        for (double x = phase; x < frames - 1; x += period) {
            const std::size_t i = static_cast<std::size_t>(x);
            total += std::max(clean[i], clean[i + 1]);
        }
        if (total > bestSum) {
            bestSum = total;
            bestPhase = phase;
        }
    }
    info.firstBeat = static_cast<TimeUs>(bestPhase * hopSec * kSecond);
    // Sicherheit: Verhältnis der Spitze zum Durchschnitt der Autokorrelation.
    double mean = 0.0;
    for (int lag = lo; lag <= hi; ++lag) mean += ac[static_cast<std::size_t>(lag)];
    mean /= (hi - lo + 1);
    info.confidence = std::clamp((ac[static_cast<std::size_t>(best)] / std::max(1e-12, mean) - 1.0) / 3.0, 0.0, 1.0);
    return info;
}

BeatInfo detectBeats(const std::string& path, TimeUs maxDuration) {
    constexpr int rate = 11025;
    AudioReader reader;
    if (!reader.open(path, rate)) return {};
    const std::size_t maxFrames = static_cast<std::size_t>(maxDuration / kSecond * rate);
    std::vector<float> stereo(8192 * 2), mono;
    mono.reserve(maxFrames);
    std::size_t got;
    while (mono.size() < maxFrames && (got = reader.read(stereo.data(), 8192)) > 0)
        for (std::size_t i = 0; i < got; ++i) mono.push_back(0.5f * (stereo[i * 2] + stereo[i * 2 + 1]));
    return detectBeatsFromSamples(mono.data(), mono.size(), rate);
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
