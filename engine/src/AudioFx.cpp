#include "AudioFx.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace loggx {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// Formeln nach dem "Audio EQ Cookbook" (R. Bristow-Johnson).
void Biquad::design(Type type, double freq, double gainDb, double q, double rate) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    double a0, nb0, nb1, nb2, na1, na2;
    switch (type) {
        case Type::Peak:
            nb0 = 1 + alpha * A;
            nb1 = -2 * cw;
            nb2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            na1 = -2 * cw;
            na2 = 1 - alpha / A;
            break;
        case Type::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            nb0 = A * ((A + 1) - (A - 1) * cw + s);
            nb1 = 2 * A * ((A - 1) - (A + 1) * cw);
            nb2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;
            na1 = -2 * ((A - 1) + (A + 1) * cw);
            na2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        default: {
            const double s = 2 * std::sqrt(A) * alpha;
            nb0 = A * ((A + 1) + (A - 1) * cw + s);
            nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
            nb2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;
            na1 = 2 * ((A - 1) - (A + 1) * cw);
            na2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }
    b0 = nb0 / a0;
    b1 = nb1 / a0;
    b2 = nb2 / a0;
    a1 = na1 / a0;
    a2 = na2 / a0;
}

// ---------------------------------------------------------------- Reverb

void Reverb::prepare(int rate) {
    if (rate == m_rate) return;
    m_rate = rate;
    static const int combTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int apTuning[4] = {556, 441, 341, 225};
    const double k = rate / 44100.0;
    for (int i = 0; i < 8; ++i) {
        m_combL[i].buf.assign(static_cast<std::size_t>(combTuning[i] * k), 0.0f);
        m_combR[i].buf.assign(static_cast<std::size_t>((combTuning[i] + 23) * k), 0.0f);
    }
    for (int i = 0; i < 4; ++i) {
        m_apL[i].buf.assign(static_cast<std::size_t>(apTuning[i] * k), 0.0f);
        m_apR[i].buf.assign(static_cast<std::size_t>((apTuning[i] + 23) * k), 0.0f);
    }
    reset();
}

void Reverb::reset() {
    for (auto* arr : {&m_combL, &m_combR})
        for (auto& c : *arr) {
            std::fill(c.buf.begin(), c.buf.end(), 0.0f);
            c.store = 0;
            c.pos = 0;
        }
    for (auto* arr : {&m_apL, &m_apR})
        for (auto& a : *arr) {
            std::fill(a.buf.begin(), a.buf.end(), 0.0f);
            a.pos = 0;
        }
}

void Reverb::process(const float* in, float* wetL, float* wetR, std::size_t frames) {
    constexpr float feedback = 0.84f, damp = 0.2f, gain = 0.015f;
    auto run = [&](std::array<Comb, 8>& combs, std::array<Allpass, 4>& aps, float* out) {
        for (std::size_t i = 0; i < frames; ++i) {
            const float x = in[i] * gain;
            float acc = 0.0f;
            for (auto& c : combs) {
                const float y = c.buf[c.pos];
                c.store = y * (1.0f - damp) + c.store * damp;
                c.buf[c.pos] = x + c.store * feedback;
                if (++c.pos >= c.buf.size()) c.pos = 0;
                acc += y;
            }
            for (auto& a : aps) {
                const float b = a.buf[a.pos];
                const float y = -acc + b;
                a.buf[a.pos] = acc + b * 0.5f;
                if (++a.pos >= a.buf.size()) a.pos = 0;
                acc = y;
            }
            out[i] = acc;
        }
    };
    run(m_combL, m_apL, wetL);
    run(m_combR, m_apR, wetR);
}

// ------------------------------------------------------- TrackFxProcessor

void TrackFxProcessor::reset() {
    m_low.reset();
    m_mid.reset();
    m_high.reset();
    m_env = 0.0;
    std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
    std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
    m_delayPos = 0;
    m_reverb.reset();
}

void TrackFxProcessor::process(float* io, std::size_t frames, const TrackEffects& fx, double bpm,
                               int rate) {
    if (rate != m_rate) {
        m_rate = rate;
        m_eqState[0] = m_eqState[1] = m_eqState[2] = 1e9;
        m_delayL.assign(static_cast<std::size_t>(rate * 2), 0.0f);
        m_delayR.assign(static_cast<std::size_t>(rate * 2), 0.0f);
        m_delayPos = 0;
        m_reverb.prepare(rate);
    }

    // ---- EQ (Filter nur neu berechnen, wenn sich ein Regler bewegt)
    const double eq[3] = {fx.eqLow, fx.eqMid, fx.eqHigh};
    if (eq[0] != m_eqState[0]) m_low.design(Biquad::Type::LowShelf, 120.0, eq[0], 0.707, rate);
    if (eq[1] != m_eqState[1]) m_mid.design(Biquad::Type::Peak, 1000.0, eq[1], 0.8, rate);
    if (eq[2] != m_eqState[2]) m_high.design(Biquad::Type::HighShelf, 6000.0, eq[2], 0.707, rate);
    for (int i = 0; i < 3; ++i) m_eqState[i] = eq[i];
    if (eq[0] != 0.0 || eq[1] != 0.0 || eq[2] != 0.0) {
        for (std::size_t i = 0; i < frames; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                float v = io[i * 2 + ch];
                if (eq[0] != 0.0) v = m_low.process(v, ch);
                if (eq[1] != 0.0) v = m_mid.process(v, ch);
                if (eq[2] != 0.0) v = m_high.process(v, ch);
                io[i * 2 + ch] = v;
            }
    }

    // ---- Kompressor (Spitzenwert-Hüllkurve, weiche Kennlinie)
    if (fx.compressor > 0.0) {
        const double amount = std::clamp(fx.compressor, 0.0, 1.0);
        const double thresholdDb = -6.0 - 24.0 * amount;
        const double ratio = 1.5 + 6.5 * amount;
        const double makeupDb = -thresholdDb * (1.0 - 1.0 / ratio) * 0.5;
        const double att = std::exp(-1.0 / (0.005 * rate));
        const double rel = std::exp(-1.0 / (0.12 * rate));
        for (std::size_t i = 0; i < frames; ++i) {
            const double peak = std::max(std::abs(io[i * 2]), std::abs(io[i * 2 + 1]));
            m_env = peak > m_env ? att * m_env + (1 - att) * peak : rel * m_env + (1 - rel) * peak;
            const double levelDb = 20.0 * std::log10(std::max(m_env, 1e-6));
            double grDb = 0.0;
            if (levelDb > thresholdDb) grDb = (thresholdDb - levelDb) * (1.0 - 1.0 / ratio);
            const float g = static_cast<float>(std::pow(10.0, (grDb + makeupDb) / 20.0));
            io[i * 2] *= g;
            io[i * 2 + 1] *= g;
        }
    }

    // ---- Delay: punktierte Achtel, Ping-Pong
    if (fx.delay > 0.0) {
        const float mix = static_cast<float>(std::clamp(fx.delay, 0.0, 1.0));
        const std::size_t len = m_delayL.size();
        const std::size_t d = std::clamp<std::size_t>(
            static_cast<std::size_t>(0.75 * 60.0 / std::max(20.0, bpm) * rate), 1, len - 1);
        for (std::size_t i = 0; i < frames; ++i) {
            const std::size_t readPos = (m_delayPos + len - d) % len;
            const float dl = m_delayL[readPos], dr = m_delayR[readPos];
            const float l = io[i * 2], r = io[i * 2 + 1];
            m_delayL[m_delayPos] = (l + r) * 0.5f + dr * 0.4f;
            m_delayR[m_delayPos] = dl * 0.4f;
            m_delayPos = (m_delayPos + 1) % len;
            io[i * 2] = l + dl * mix;
            io[i * 2 + 1] = r + dr * mix;
        }
    }

    // ---- Hall
    if (fx.reverb > 0.0) {
        const float wet = static_cast<float>(std::clamp(fx.reverb, 0.0, 1.0));
        m_mono.resize(frames);
        m_wetL.resize(frames);
        m_wetR.resize(frames);
        for (std::size_t i = 0; i < frames; ++i) m_mono[i] = (io[i * 2] + io[i * 2 + 1]) * 0.5f;
        m_reverb.process(m_mono.data(), m_wetL.data(), m_wetR.data(), frames);
        const float dry = 1.0f - 0.4f * wet;
        for (std::size_t i = 0; i < frames; ++i) {
            io[i * 2] = io[i * 2] * dry + m_wetL[i] * wet * 3.0f;
            io[i * 2 + 1] = io[i * 2 + 1] * dry + m_wetR[i] * wet * 3.0f;
        }
    }
}

}  // namespace loggx
