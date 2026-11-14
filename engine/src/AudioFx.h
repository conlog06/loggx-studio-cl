// Audioeffekte der Spuren: 3-Band-EQ, Kompressor, Delay, Hall – und der
// Limiter am Master. Alle arbeiten blockweise auf Stereo-Puffern.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "loggx/core/Project.h"

namespace loggx {

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0}, z2[2] = {0, 0};

    enum class Type { LowShelf, Peak, HighShelf };
    void design(Type type, double freq, double gainDb, double q, double rate);
    float process(float x, int ch) {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

// Freeverb-ähnlicher Hall (Kammfilter + Allpässe).
class Reverb {
public:
    void prepare(int rate);
    void reset();
    void process(const float* in, float* wetL, float* wetR, std::size_t frames);

private:
    struct Comb {
        std::vector<float> buf;
        std::size_t pos = 0;
        float store = 0;
    };
    struct Allpass {
        std::vector<float> buf;
        std::size_t pos = 0;
    };
    std::array<Comb, 8> m_combL, m_combR;
    std::array<Allpass, 4> m_apL, m_apR;
    int m_rate = 0;
};

class TrackFxProcessor {
public:
    void reset();
    // Bearbeitet den Stereo-Puffer (interleaved) an Ort und Stelle.
    void process(float* interleaved, std::size_t frames, const TrackEffects& fx, double bpm,
                 int rate);

private:
    Biquad m_low, m_mid, m_high;
    double m_eqState[3] = {1e9, 1e9, 1e9};
    int m_rate = 0;
    double m_env = 0.0;           // Kompressor-Hüllkurve (linear)
    std::vector<float> m_delayL, m_delayR;
    std::size_t m_delayPos = 0;
    Reverb m_reverb;
    std::vector<float> m_mono, m_wetL, m_wetR;
};

}  // namespace loggx
