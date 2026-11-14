// Eingebaute Instrumente. Jede Note ist eine reine Funktion der Zeit seit
// dem Anschlag – ohne inneren Zustand. So kann die Wiedergabe an jeder
// Stelle beginnen und Export und Vorschau klingen garantiert gleich.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "loggx/engine/Engine.h"

namespace loggx::synth {

namespace {

constexpr double kTwoPi = 6.283185307179586;

double midiToHz(double pitch) { return 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0); }

// Deterministisches Rauschen -1..1 aus einer Ganzzahl.
inline float noise(std::uint64_t n) {
    n ^= n >> 33;
    n *= 0xff51afd7ed558ccdULL;
    n ^= n >> 33;
    n *= 0xc4ceb9fe1a85ec53ULL;
    n ^= n >> 33;
    return static_cast<float>(static_cast<double>(n >> 11) / 4503599627370496.0 - 1.0);
}

struct Adsr {
    double attack, decay, sustain, release;
};

// Hüllkurve: t Sekunden seit Anschlag, held Sekunden bis zum Loslassen.
double envelope(const Adsr& e, double t, double held) {
    auto level = [&](double x) {
        if (x < e.attack) return x / e.attack;
        const double d = x - e.attack;
        return e.sustain + (1.0 - e.sustain) * std::exp(-d / std::max(1e-4, e.decay));
    };
    if (t < held) return level(t);
    const double startLevel = level(held);
    const double r = (t - held) / e.release;
    if (r >= 1.0) return 0.0;
    return startLevel * (1.0 - r) * (1.0 - r);
}

// Additive Sägezahn-ähnliche Welle mit begrenzten Obertönen (weich).
double additive(double phaseCycles, double f, int maxHarmonics, double rolloff, int sampleRate) {
    double v = 0.0;
    const double nyquist = sampleRate * 0.45;
    for (int k = 1; k <= maxHarmonics; ++k) {
        if (k * f > nyquist) break;
        v += std::sin(kTwoPi * phaseCycles * k) / k * std::exp(-(k - 1) / rolloff);
    }
    return v;
}

// Tonhöhe mit Abklingen von f0 auf f1 (Zeitkonstante tau): Phase analytisch.
double sweepPhase(double t, double f0, double f1, double tau) {
    return f1 * t + (f0 - f1) * tau * (1.0 - std::exp(-t / tau));
}

float drumSample(int pitch, double t, std::int64_t n, std::uint64_t seed) {
    const auto noiseAt = [&](std::int64_t i) { return noise(seed + static_cast<std::uint64_t>(i)); };
    // Hochpass-ähnliches Rauschen (Differenz zweier Nachbarn).
    const auto hiss = [&]() { return 0.5f * (noiseAt(n) - noiseAt(n - 1)); };
    switch (pitch) {
        case 35:
        case 36: {   // Bassdrum
            const double ph = sweepPhase(t, 160.0, 48.0, 0.035);
            return static_cast<float>(std::sin(kTwoPi * ph) * std::exp(-t / 0.28) +
                                      0.3 * noiseAt(n) * std::exp(-t / 0.004));
        }
        case 38:
        case 40: {   // Snare
            const double tone = std::sin(kTwoPi * sweepPhase(t, 240.0, 185.0, 0.02)) * std::exp(-t / 0.08);
            return static_cast<float>(0.45 * tone + 0.75 * noiseAt(n) * std::exp(-t / 0.13));
        }
        case 39: {   // Händeklatschen: drei kurze Stöße, dann Ausklang
            double env = std::exp(-t / 0.12);
            for (double s : {0.0, 0.011, 0.022})
                if (t >= s) env += 0.8 * std::exp(-(t - s) / 0.004);
            return static_cast<float>(0.5 * hiss() * env * 2.0);
        }
        case 42:
        case 44:     // geschlossene Hi-Hat
            return static_cast<float>(1.1 * hiss() * std::exp(-t / 0.035));
        case 46:     // offene Hi-Hat
            return static_cast<float>(0.9 * hiss() * std::exp(-t / 0.28));
        case 49:
        case 57:     // Becken
            return static_cast<float>(0.8 * hiss() * std::exp(-t / 1.1));
        case 51:
        case 59: {   // Ride
            const double bell = std::sin(kTwoPi * 610.0 * t) * std::sin(kTwoPi * 1630.0 * t);
            return static_cast<float>((0.5 * hiss() + 0.25 * bell) * std::exp(-t / 0.7));
        }
        case 41: case 43: case 45: case 47: case 48: case 50: {   // Toms
            const double base = 70.0 + (pitch - 41) * 18.0;
            const double ph = sweepPhase(t, base * 1.6, base, 0.05);
            return static_cast<float>(0.9 * std::sin(kTwoPi * ph) * std::exp(-t / 0.3));
        }
        default: {   // Klick / Perkussion
            const double f = midiToHz(pitch);
            return static_cast<float>(0.6 * std::sin(kTwoPi * f * t) * std::exp(-t / 0.06) +
                                      0.2 * noiseAt(n) * std::exp(-t / 0.01));
        }
    }
}

}  // namespace

double releaseTime(InstrumentType type) {
    switch (type) {
        case InstrumentType::Piano: return 0.35;
        case InstrumentType::ElectricPiano: return 0.4;
        case InstrumentType::Synth: return 0.2;
        case InstrumentType::Bass: return 0.1;
        case InstrumentType::Pad: return 1.4;
        case InstrumentType::Strings: return 0.6;
        case InstrumentType::Drums: return 1.5;   // Ausklang unabhängig von der Notenlänge
    }
    return 0.3;
}

void renderNote(InstrumentType type, int pitch, double velocity, std::int64_t held,
                std::int64_t from, float* out, std::size_t frames, int sampleRate) {
    const double sr = sampleRate;
    const double heldS = static_cast<double>(held) / sr;
    const double amp = 0.32 * std::pow(std::clamp(velocity, 0.0, 1.0), 1.3);
    const double f = midiToHz(pitch);
    const std::uint64_t seed = static_cast<std::uint64_t>(pitch) * 0x9E3779B97F4A7C15ULL;

    for (std::size_t i = 0; i < frames; ++i) {
        const std::int64_t n = from + static_cast<std::int64_t>(i);
        if (n < 0) continue;
        const double t = static_cast<double>(n) / sr;
        double v = 0.0;
        switch (type) {
            case InstrumentType::Piano: {
                // Obertöne mit leichter Inharmonizität, hohe klingen schneller ab.
                const double baseDecay = 2.2 * std::pow(261.6 / f, 0.45);
                for (int k = 1; k <= 8; ++k) {
                    const double fk = k * f * std::sqrt(1.0 + 0.0004 * k * k);
                    if (fk > sr * 0.45) break;
                    v += std::sin(kTwoPi * fk * t) / std::pow(k, 1.4) *
                         std::exp(-t * k / (baseDecay * 2.0)) ;
                }
                v *= std::exp(-t / baseDecay) * 0.8;
                v *= envelope({0.002, 10.0, 1.0, 0.35}, t, heldS);
                break;
            }
            case InstrumentType::ElectricPiano: {
                // Modulationsindex klein halten: ab etwa 2,4 löscht FM den
                // Grundton aus (Bessel-Nullstelle) und die Note klingt eine Oktave höher.
                const double index = 1.1 * velocity * std::exp(-t * 2.5) + 0.15;
                v = 0.75 * std::sin(kTwoPi * f * t + index * std::sin(kTwoPi * f * t)) +
                    0.35 * std::sin(kTwoPi * f * t);
                v += 0.15 * std::sin(kTwoPi * 14.0 * f * t) * std::exp(-t * 25.0);   // Anschlag
                v *= std::exp(-t / 2.5) * envelope({0.002, 10.0, 1.0, 0.4}, t, heldS);
                break;
            }
            case InstrumentType::Synth: {
                const double a = additive(f * t, f, 24, 6.0, sampleRate);
                const double b = additive(f * 1.006 * t, f * 1.006, 24, 6.0, sampleRate);
                v = 0.45 * (a + b) * envelope({0.01, 0.25, 0.7, 0.2}, t, heldS);
                break;
            }
            case InstrumentType::Bass: {
                // Obertöne klingen schneller ab als der Grundton (Zupfen).
                double s = 0.0;
                for (int k = 1; k <= 10; ++k) {
                    if (k * f > sr * 0.45) break;
                    s += std::sin(kTwoPi * k * f * t) / k * std::exp(-t * (k - 1) * 6.0);
                }
                v = (0.8 * s + 0.5 * std::sin(kTwoPi * f * t)) * envelope({0.004, 0.4, 0.65, 0.1}, t, heldS);
                break;
            }
            case InstrumentType::Pad: {
                const double tremolo = 1.0 + 0.08 * std::sin(kTwoPi * 0.3 * t);
                v = 0.0;
                for (double det : {-0.008, 0.0, 0.0075})
                    v += additive(f * (1.0 + det) * t, f, 12, 3.5, sampleRate);
                v *= 0.3 * tremolo * envelope({0.6, 1.0, 0.85, 1.4}, t, heldS);
                break;
            }
            case InstrumentType::Strings: {
                // Vibrato setzt verzögert ein; Phase analytisch integriert.
                const double depth = 0.004 * std::clamp((t - 0.3) / 0.4, 0.0, 1.0);
                const double fv = 5.2;
                const double phase = t + depth * (1.0 - std::cos(kTwoPi * fv * t)) / (kTwoPi * fv);
                v = 0.0;
                for (double det : {-0.004, 0.004})
                    v += additive(f * (1.0 + det) * phase, f, 16, 5.0, sampleRate);
                v *= 0.35 * envelope({0.25, 1.0, 0.9, 0.6}, t, heldS);
                break;
            }
            case InstrumentType::Drums:
                v = drumSample(pitch, t, n, seed);
                if (t > 3.0) v = 0.0;
                break;
        }
        out[i] += static_cast<float>(v * amp);
    }
}

}  // namespace loggx::synth
