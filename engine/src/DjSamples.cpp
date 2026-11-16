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

}  // namespace loggx
