// Eingebaute Sampler-Klänge für den DJ-Bereich – rein synthetisch erzeugt,
// damit keine Rechte an fremden Aufnahmen nötig sind.
#include <algorithm>
#include <cmath>
#include <random>

#include "loggx/engine/Dj.h"

namespace loggx {

namespace {
constexpr double kTau = 6.283185307179586;
}

const char* djSampleName(int kind) {
    static const char* names[8] = {"Air Horn", "Sirene", "Riser", "Downlifter", "Laser", "Sub Drop", "Clap", "Scratch"};
    return kind >= 0 && kind < 8 ? names[kind] : "";
}

std::vector<float> synthDjSample(int kind, int rate) {
    const double r = rate;
    std::mt19937 rng(static_cast<unsigned>(kind) * 7919u + 1u);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    double seconds = 1.0;
    switch (kind) {
    case 0: seconds = 1.6; break;
    case 1: seconds = 2.0; break;
    case 2: seconds = 4.0; break;
    case 3: seconds = 2.5; break;
    case 4: seconds = 0.6; break;
    case 5: seconds = 2.0; break;
    case 6: seconds = 0.35; break;
    default: seconds = 0.7; break;
    }
    const std::size_t n = static_cast<std::size_t>(seconds * r);
    std::vector<float> out(n * 2, 0.0f);
    double ph[4] = {0, 0, 0, 0};
    float lp = 0, lp2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / r;
        const double u = t / seconds;   // 0…1
        double v = 0;
        switch (kind) {
        case 0: {   // Air Horn: drei Stöße, Sägezahn-Akkord mit Vibrato
            const double seg = std::fmod(t, 0.533);
            const double gate = t < 1.6 && seg < (t < 1.07 ? 0.22 : 0.5) ? std::min(1.0, seg * 80) : 0.0;
            const double vib = 1.0 + 0.012 * std::sin(kTau * 6.0 * t);
            const double f[3] = {415.3, 554.4, 659.3};
            for (int k = 0; k < 3; ++k) {
                ph[k] = std::fmod(ph[k] + f[k] * vib / r, 1.0);
                v += (2.0 * ph[k] - 1.0) / 3.0;
            }
            v = std::tanh(v * 2.2) * gate * 0.55;
            break;
        }
        case 1: {   // Sirene: auf- und abschwellend
            const double f = 700 + 500 * std::sin(kTau * 1.5 * t);
            ph[0] = std::fmod(ph[0] + f / r, 1.0);
            v = std::sin(kTau * ph[0]) * 0.5 * std::min(1.0, (seconds - t) * 8) * std::min(1.0, t * 20);
            break;
        }
        case 2: {   // Riser: Rauschen mit steigendem Filter + Ton
            const float x = noise(rng);
            const float a = static_cast<float>(0.02 + 0.5 * u * u);
            lp += a * (x - lp);
            const double f = 200 * std::pow(8.0, u);
            ph[0] = std::fmod(ph[0] + f / r, 1.0);
            v = (lp * 0.6 + 0.25 * std::sin(kTau * ph[0])) * u * u * std::min(1.0, (seconds - t) * 30);
            break;
        }
        case 3: {   // Downlifter: fallendes Rauschen
            const float x = noise(rng);
            const float a = static_cast<float>(0.5 * (1 - u) * (1 - u) + 0.01);
            lp += a * (x - lp);
            v = lp * 0.6 * (1 - u) * std::min(1.0, t * 50);
            break;
        }
        case 4: {   // Laser: schnell fallender Ton, dreimal
            const double seg = std::fmod(t, 0.2);
            const double f = 3000 * std::exp(-seg * 22) + 120;
            ph[0] = std::fmod(ph[0] + f / r, 1.0);
            v = (ph[0] < 0.5 ? 0.35 : -0.35) * std::exp(-seg * 8);
            break;
        }
        case 5: {   // Sub Drop: tiefer, fallender Sinus
            const double f = 30 + 90 * std::exp(-t * 2.2);
            ph[0] = std::fmod(ph[0] + f / r, 1.0);
            v = std::tanh(1.6 * std::sin(kTau * ph[0])) * 0.8 * std::exp(-t * 1.2) * std::min(1.0, t * 200);
            break;
        }
        case 6: {   // Clap: drei kurze Rauschstöße + Nachklang
            const float x = noise(rng);
            const float hp = x - lp;
            lp = x;
            double env = 0;
            for (double s0 : {0.0, 0.011, 0.022}) env = std::max(env, t >= s0 ? std::exp(-(t - s0) * 160) : 0.0);
            env = std::max(env, t >= 0.03 ? 0.5 * std::exp(-(t - 0.03) * 18) : 0.0);
            v = hp * env * 0.7;
            break;
        }
        default: {   // Scratch: vor/zurück gezogenes, gefiltertes Rauschen mit Ton
            const double speed = std::sin(kTau * 3.0 * t);
            const float x = noise(rng);
            const float a = static_cast<float>(0.05 + 0.4 * std::abs(speed));
            lp += a * (x - lp);
            lp2 += 0.3f * (lp - lp2);
            ph[0] = std::fmod(ph[0] + (180 + 500 * std::abs(speed)) / r, 1.0);
            v = (lp2 * 0.8 + 0.25 * std::sin(kTau * ph[0])) * std::abs(speed) * std::min(1.0, (seconds - t) * 40);
            break;
        }
        }
        const float s = static_cast<float>(std::clamp(v, -1.0, 1.0));
        out[i * 2] = s;
        out[i * 2 + 1] = s;
    }
    return out;
}

std::vector<DjPhrase> analyzeDjPhrases(const DjTrack& track, double bpm, double firstBeat) {
    std::vector<DjPhrase> out;
    const double duration = track.duration();
    if (bpm <= 0 || track.wave.empty() || duration < 20) return out;
    const double block = 60.0 / bpm * 4 * 8;   // 8 Takte
    // Erster Block beginnt am ersten Taktanfang (vor dem Raster: Vorlauf)
    double t0 = firstBeat;
    while (t0 - block > 0) t0 -= block;
    std::vector<double> starts;
    if (t0 > 0.5) starts.push_back(0.0);
    for (double t = t0; t < duration - 1.0; t += block) starts.push_back(t);
    const std::size_t n = starts.size();
    std::vector<double> energy(n, 0), bass(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const double a = starts[i], b = i + 1 < n ? starts[i + 1] : duration;
        const std::size_t c0 = static_cast<std::size_t>(a * DjTrack::kWaveRate);
        const std::size_t c1 = std::min(track.wave.size(), static_cast<std::size_t>(b * DjTrack::kWaveRate));
        double e = 0, l = 0;
        for (std::size_t c = c0; c < c1; ++c) {
            e += track.wave[c][0];
            l += track.wave[c][1];
        }
        const double cnt = static_cast<double>(std::max<std::size_t>(1, c1 - c0));
        energy[i] = e / cnt;
        bass[i] = l / cnt;
    }
    const double maxE = std::max(1.0, *std::max_element(energy.begin(), energy.end()));
    const double maxB = std::max(1.0, *std::max_element(bass.begin(), bass.end()));
    std::vector<DjPhraseKind> kind(n, DjPhraseKind::Down);
    for (std::size_t i = 0; i < n; ++i) {
        const double e = energy[i] / maxE, b = bass[i] / maxB;
        kind[i] = (b > 0.62 && e > 0.6) ? DjPhraseKind::Chorus : DjPhraseKind::Down;
    }
    // Vor einem Chorus: Aufbau (Up), sofern nicht selbst Chorus
    for (std::size_t i = 0; i + 1 < n; ++i)
        if (kind[i] == DjPhraseKind::Down && kind[i + 1] == DjPhraseKind::Chorus) kind[i] = DjPhraseKind::Up;
    const auto firstChorus = std::find(kind.begin(), kind.end(), DjPhraseKind::Chorus);
    const auto lastChorus = std::find(kind.rbegin(), kind.rend(), DjPhraseKind::Chorus);
    if (firstChorus != kind.end()) {
        for (auto it = kind.begin(); it != firstChorus; ++it)
            if (*it != DjPhraseKind::Up) *it = DjPhraseKind::Intro;
        for (auto it = kind.rbegin(); it != lastChorus; ++it) *it = DjPhraseKind::Outro;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double a = starts[i], b = i + 1 < n ? starts[i + 1] : duration;
        if (!out.empty() && out.back().kind == kind[i]) out.back().end = b;
        else out.push_back({a, b, kind[i]});
    }
    return out;
}

}  // namespace loggx
