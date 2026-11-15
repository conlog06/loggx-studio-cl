// Spektrale Analyse: Tonart-Erkennung und Stimmentrennung (ohne KI).
//
// Stimmentrennung: Gesang liegt in fast jeder Produktion in der Mitte des
// Stereobilds. Pro Frequenz und Zeitabschnitt (STFT) wird gemessen, wie
// ähnlich linker und rechter Kanal sind; was in der Mitte liegt, im
// Gesangsbereich (ca. 120 Hz – 12 kHz) und eher tonal als perkussiv ist
// (Median-Filter, „HPSS“), kommt in die Gesangsspur. Instrumental = Original
// minus Gesang – beide zusammen ergeben exakt das Original.
#include "loggx/engine/Engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#include "Parallel.h"

namespace loggx {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cpx = std::complex<float>;

// Iterative Radix-2-FFT.
class Fft {
public:
    explicit Fft(int n) : m_n(n), m_rev(static_cast<std::size_t>(n)), m_tw(static_cast<std::size_t>(n / 2)) {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            m_rev[static_cast<std::size_t>(i)] = r;
        }
        for (int i = 0; i < n / 2; ++i)
            m_tw[static_cast<std::size_t>(i)] = std::polar(1.0f, static_cast<float>(-2.0 * kPi * i / n));
    }
    int size() const { return m_n; }
    void forward(cpx* x) const { run(x, false); }
    void inverse(cpx* x) const {   // ohne 1/n
        run(x, true);
    }

private:
    void run(cpx* x, bool inv) const {
        for (int i = 0; i < m_n; ++i) {
            const int r = m_rev[static_cast<std::size_t>(i)];
            if (i < r) std::swap(x[i], x[r]);
        }
        for (int len = 2; len <= m_n; len <<= 1) {
            const int half = len / 2, step = m_n / len;
            for (int i = 0; i < m_n; i += len)
                for (int j = 0; j < half; ++j) {
                    cpx w = m_tw[static_cast<std::size_t>(j * step)];
                    if (inv) w = std::conj(w);
                    const cpx u = x[i + j], v = x[i + j + half] * w;
                    x[i + j] = u + v;
                    x[i + j + half] = u - v;
                }
        }
    }
    int m_n;
    std::vector<int> m_rev;
    std::vector<cpx> m_tw;
};

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Krumhansl-Kessler-Profile (Dur, Moll)
constexpr std::array<double, 12> kMajor = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double correlate(const std::array<double, 12>& chroma, const std::array<double, 12>& profile, int shift) {
    double mc = 0, mp = 0;
    for (int i = 0; i < 12; ++i) {
        mc += chroma[static_cast<std::size_t>(i)];
        mp += profile[static_cast<std::size_t>(i)];
    }
    mc /= 12;
    mp /= 12;
    double num = 0, dc = 0, dp = 0;
    for (int i = 0; i < 12; ++i) {
        const double c = chroma[static_cast<std::size_t>((i + shift) % 12)] - mc;
        const double p = profile[static_cast<std::size_t>(i)] - mp;
        num += c * p;
        dc += c * c;
        dp += p * p;
    }
    return dc > 0 && dp > 0 ? num / std::sqrt(dc * dp) : 0.0;
}

}  // namespace

// ------------------------------------------------------------------ Tonart

std::string keyName(int tonic, bool minor) {
    static const char* major[] = {"C", "Cis", "D", "Es", "E", "F", "Fis", "G", "As", "A", "B", "H"};
    static const char* minorN[] = {"c", "cis", "d", "es", "e", "f", "fis", "g", "gis", "a", "b", "h"};
    if (tonic < 0 || tonic > 11) return "?";
    return std::string(minor ? minorN[tonic] : major[tonic]) + (minor ? "-Moll" : "-Dur");
}

KeyInfo detectKeyFromSamples(const float* mono, std::size_t count, int sampleRate) {
    KeyInfo info;
    const int n = 8192;
    if (!mono || count < static_cast<std::size_t>(n) || sampleRate <= 0) return info;
    const int hop = n / 2;
    const int frames = static_cast<int>((count - n) / hop) + 1;
    Fft fft(n);
    std::vector<float> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * i / n));

    // Bin → Tonklasse (55 Hz … 2 kHz)
    std::vector<int> pitchClass(static_cast<std::size_t>(n / 2), -1);
    for (int k = 1; k < n / 2; ++k) {
        const double f = static_cast<double>(k) * sampleRate / n;
        if (f < 55.0 || f > 2000.0) continue;
        const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(f / 440.0)));
        pitchClass[static_cast<std::size_t>(k)] = ((midi % 12) + 12) % 12;
    }

    std::array<double, 12> chroma{};
    std::mutex lock;
    parallelRows(0, frames, [&](int f0, int f1) {
        std::vector<cpx> buf(static_cast<std::size_t>(n));
        std::array<double, 12> local{};
        for (int f = f0; f < f1; ++f) {
            const float* src = mono + static_cast<std::size_t>(f) * hop;
            for (int i = 0; i < n; ++i) buf[static_cast<std::size_t>(i)] = cpx(src[i] * window[static_cast<std::size_t>(i)], 0.0f);
            fft.forward(buf.data());
            std::array<double, 12> frame{};
            for (int k = 1; k < n / 2; ++k) {
                const int pc = pitchClass[static_cast<std::size_t>(k)];
                if (pc >= 0) frame[static_cast<std::size_t>(pc)] += std::abs(buf[static_cast<std::size_t>(k)]);
            }
            // Jeden Abschnitt normieren, damit laute Stellen nicht dominieren.
            double sum = 0;
            for (double v : frame) sum += v;
            if (sum > 1e-9)
                for (int i = 0; i < 12; ++i) local[static_cast<std::size_t>(i)] += frame[static_cast<std::size_t>(i)] / sum;
        }
        std::lock_guard<std::mutex> g(lock);
        for (int i = 0; i < 12; ++i) chroma[static_cast<std::size_t>(i)] += local[static_cast<std::size_t>(i)];
    }, 8);

    double best = -2, second = -2;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int minor = 0; minor < 2; ++minor) {
            const double r = correlate(chroma, minor ? kMinor : kMajor, tonic);
            if (r > best) {
                second = best;
                best = r;
                info.tonic = tonic;
                info.minor = minor != 0;
            } else if (r > second) {
                second = r;
            }
        }
    // Sicherheit: Korrelation und Abstand zur zweitbesten Tonart
    info.confidence = std::clamp(best * 0.6 + (best - second) * 4.0, 0.0, 1.0);
    if (best < 0.2) info.tonic = -1;
    return info;
}

KeyInfo detectKey(const std::string& path, TimeUs maxDuration) {
    constexpr int kRate = 22050;
    AudioReader reader;
    if (!reader.open(path, kRate)) return {};
    const std::size_t maxFrames = static_cast<std::size_t>(maxDuration / kSecond * kRate);
    std::vector<float> mono;
    std::vector<float> buf(4096 * 2);
    while (mono.size() < maxFrames) {
        const std::size_t got = reader.read(buf.data(), 4096);
        for (std::size_t i = 0; i < got; ++i) mono.push_back(0.5f * (buf[i * 2] + buf[i * 2 + 1]));
        if (got < 4096) break;
    }
    return detectKeyFromSamples(mono.data(), mono.size(), kRate);
}

// ------------------------------------------------------- Stimmentrennung

SeparationResult separateVocalsBuffer(const std::vector<float>& stereo, int sampleRate, std::vector<float>& vocals,
                                      std::vector<float>& instrumental, const std::function<void(double)>& progress,
                                      const std::atomic<bool>* cancel) {
    SeparationResult result;
    const std::size_t len = stereo.size() / 2;
    vocals.assign(len * 2, 0.0f);
    instrumental = stereo;
    if (len < 4096) {
        result.error = "Zu kurz";
        return result;
    }

    // Mono? Dann gibt es keine Mitte, die sich vom Rest unterscheidet.
    double mid = 0, side = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const double l = stereo[i * 2], r = stereo[i * 2 + 1];
        mid += (l + r) * (l + r);
        side += (l - r) * (l - r);
    }
    if (side < mid * 1e-5) {
        result.mono = true;
        result.error = "Die Datei ist mono – Gesang lässt sich ohne KI nur aus Stereo-Aufnahmen trennen";
        return result;
    }

    const int n = sampleRate > 30000 ? 4096 : 2048;
    const int hop = n / 4;
    const int bins = n / 2 + 1;
    const int frames = static_cast<int>((len + n) / hop) + 1;
    Fft fft(n);
    std::vector<float> win(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) win[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(kPi * (i + 0.5) / n));   // √Hann

    // Gewichtung nach Frequenz: Gesangsbereich
    std::vector<float> band(static_cast<std::size_t>(bins));
    for (int k = 0; k < bins; ++k) {
        const float f = static_cast<float>(k) * sampleRate / n;
        band[static_cast<std::size_t>(k)] = smoothstep(90.0f, 170.0f, f) * (1.0f - smoothstep(11000.0f, 16000.0f, f));
    }

    auto sample = [&](std::int64_t i, int ch) -> float {
        return i < 0 || i >= static_cast<std::int64_t>(len) ? 0.0f : stereo[static_cast<std::size_t>(i) * 2 + ch];
    };
    // Spektren von L und R mit einer FFT (L + iR) und Mitten-Maske.
    auto analyse = [&](int f, std::vector<cpx>& z, std::vector<cpx>& midSpec, std::vector<float>& mask) {
        const std::int64_t start = static_cast<std::int64_t>(f) * hop - n;
        for (int i = 0; i < n; ++i) {
            const float w = win[static_cast<std::size_t>(i)];
            z[static_cast<std::size_t>(i)] = cpx(sample(start + i, 0) * w, sample(start + i, 1) * w);
        }
        fft.forward(z.data());
        for (int k = 0; k < bins; ++k) {
            const cpx zk = z[static_cast<std::size_t>(k)];
            const cpx zn = std::conj(z[static_cast<std::size_t>((n - k) % n)]);
            const cpx L = (zk + zn) * 0.5f;
            const cpx R = (zk - zn) * cpx(0.0f, -0.5f);
            const float pl = std::norm(L), pr = std::norm(R);
            const float psi = pl + pr > 1e-12f ? 2.0f * (L.real() * R.real() + L.imag() * R.imag()) / (pl + pr) : 0.0f;
            float m = std::clamp((psi - 0.55f) / 0.4f, 0.0f, 1.0f);
            m = m * std::sqrt(m) * band[static_cast<std::size_t>(k)];
            midSpec[static_cast<std::size_t>(k)] = (L + R) * 0.5f;
            mask[static_cast<std::size_t>(k)] = m;
        }
    };

    // Durchgang 1: Betrag des Mitten-Anteils für den Median-Filter
    std::vector<float> mag(static_cast<std::size_t>(frames) * bins);
    std::atomic<int> done{0};
    std::atomic<bool> stop{false};
    parallelRows(0, frames, [&](int f0, int f1) {
        std::vector<cpx> z(static_cast<std::size_t>(n)), midSpec(static_cast<std::size_t>(bins));
        std::vector<float> mask(static_cast<std::size_t>(bins));
        for (int f = f0; f < f1 && !stop; ++f) {
            analyse(f, z, midSpec, mask);
            float* row = mag.data() + static_cast<std::size_t>(f) * bins;
            for (int k = 0; k < bins; ++k) row[k] = mask[static_cast<std::size_t>(k)] * std::abs(midSpec[static_cast<std::size_t>(k)]);
            if (cancel && cancel->load()) stop = true;
        }
        const int d = done.fetch_add(f1 - f0) + (f1 - f0);
        if (progress) progress(0.45 * d / frames);
    }, 64);
    if (stop) {
        result.error = "Abgebrochen";
        return result;
    }

    // Durchgang 2: tonal (Median über die Zeit) gegen perkussiv (Median über
    // die Frequenz), Gesang zurück in den Zeitbereich.
    constexpr int kMed = 8;   // ±8 Abschnitte / Bins
    const int chunks = std::max(1, std::min(frames / 64, detail::parallelCores() * 4));
    const int perChunk = (frames + chunks - 1) / chunks;
    std::vector<std::vector<float>> partial(static_cast<std::size_t>(chunks));
    done = 0;
    parallelRows(0, chunks, [&](int c0, int c1) {
        std::vector<cpx> z(static_cast<std::size_t>(n)), midSpec(static_cast<std::size_t>(bins)), v(static_cast<std::size_t>(n));
        std::vector<float> mask(static_cast<std::size_t>(bins)), med(2 * kMed + 1);
        for (int c = c0; c < c1 && !stop; ++c) {
            const int fa = c * perChunk, fb = std::min(frames, fa + perChunk);
            if (fa >= fb) continue;
            auto& out = partial[static_cast<std::size_t>(c)];
            out.assign(static_cast<std::size_t>(fb - fa) * hop + n, 0.0f);
            for (int f = fa; f < fb; ++f) {
                analyse(f, z, midSpec, mask);
                const float* row = mag.data() + static_cast<std::size_t>(f) * bins;
                for (int k = 0; k < bins; ++k) {
                    float g = mask[static_cast<std::size_t>(k)];
                    if (g > 0.0f) {
                        int m = 0;
                        for (int t = std::max(0, f - kMed); t <= std::min(frames - 1, f + kMed); ++t)
                            med[static_cast<std::size_t>(m++)] = mag[static_cast<std::size_t>(t) * bins + k];
                        std::nth_element(med.begin(), med.begin() + m / 2, med.begin() + m);
                        const float harm = med[static_cast<std::size_t>(m / 2)];
                        m = 0;
                        for (int q = std::max(0, k - kMed); q <= std::min(bins - 1, k + kMed); ++q) med[static_cast<std::size_t>(m++)] = row[q];
                        std::nth_element(med.begin(), med.begin() + m / 2, med.begin() + m);
                        const float perc = med[static_cast<std::size_t>(m / 2)];
                        const float h2 = harm * harm, p2 = perc * perc;
                        g *= h2 + p2 > 1e-20f ? h2 / (h2 + p2) : 0.0f;
                    }
                    v[static_cast<std::size_t>(k)] = midSpec[static_cast<std::size_t>(k)] * g;
                }
                for (int k = 1; k < n / 2; ++k) v[static_cast<std::size_t>(n - k)] = std::conj(v[static_cast<std::size_t>(k)]);
                fft.inverse(v.data());
                // Überlappend addieren (√Hann · √Hann = Hann, Summe bei 75 % Überlappung = 2)
                const float norm = 0.5f / n;
                float* dst = out.data() + static_cast<std::size_t>(f - fa) * hop;
                for (int i = 0; i < n; ++i) dst[i] += v[static_cast<std::size_t>(i)].real() * win[static_cast<std::size_t>(i)] * norm;
            }
            const int d = done.fetch_add(fb - fa) + (fb - fa);
            if (progress) progress(0.45 + 0.5 * d / frames);
            if (cancel && cancel->load()) stop = true;
        }
    }, 1);
    if (stop) {
        result.error = "Abgebrochen";
        return result;
    }

    // Zusammensetzen (Abschnitt f beginnt bei Sample f·hop − n)
    double eV = 0, eAll = 0;
    for (int c = 0; c < chunks; ++c) {
        const auto& out = partial[static_cast<std::size_t>(c)];
        if (out.empty()) continue;
        const std::int64_t base = static_cast<std::int64_t>(c) * perChunk * hop - n;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const std::int64_t pos = base + static_cast<std::int64_t>(i);
            if (pos < 0 || pos >= static_cast<std::int64_t>(len)) continue;
            vocals[static_cast<std::size_t>(pos) * 2] += out[i];
        }
    }
    for (std::size_t i = 0; i < len; ++i) {
        const float vv = vocals[i * 2];
        vocals[i * 2 + 1] = vv;
        instrumental[i * 2] = stereo[i * 2] - vv;
        instrumental[i * 2 + 1] = stereo[i * 2 + 1] - vv;
        eV += 2.0 * vv * vv;
        eAll += stereo[i * 2] * stereo[i * 2] + stereo[i * 2 + 1] * stereo[i * 2 + 1];
    }
    result.vocalShare = eAll > 0 ? eV / eAll : 0.0;
    result.ok = true;
    if (progress) progress(1.0);
    return result;
}

SeparationResult separateVocals(const std::string& input, const std::string& vocalsPath,
                                const std::string& instrumentalPath, int sampleRate,
                                const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    SeparationResult result;
    AudioReader reader;
    std::string error;
    if (!reader.open(input, sampleRate, &error)) {
        result.error = error.empty() ? "Datei nicht lesbar" : error;
        return result;
    }
    // Ganze Datei laden (höchstens 30 Minuten)
    const std::size_t maxFrames = static_cast<std::size_t>(sampleRate) * 60 * 30;
    std::vector<float> stereo;
    std::vector<float> buf(8192 * 2);
    for (;;) {
        const std::size_t got = reader.read(buf.data(), 8192);
        stereo.insert(stereo.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * 2));
        if (got < 8192) break;
        if (stereo.size() / 2 > maxFrames) {
            result.error = "Länger als 30 Minuten – bitte zuerst kürzen";
            return result;
        }
    }
    std::vector<float> vocals, inst;
    result = separateVocalsBuffer(stereo, sampleRate, vocals, inst, progress, cancel);
    if (!result.ok) return result;

    auto write = [&](const std::string& path, const std::vector<float>& data) {
        ExportSettings s;
        s.path = path;
        s.video = false;
        s.audioBits = 24;
        s.sampleRate = sampleRate;
        Exporter e;
        std::string err;
        if (!e.open(s, &err)) return err.empty() ? std::string("Datei kann nicht geschrieben werden") : err;
        for (std::size_t i = 0; i < data.size() / 2; i += 4096) {
            const std::size_t count = std::min<std::size_t>(4096, data.size() / 2 - i);
            if (!e.writeAudio(data.data() + i * 2, count)) return std::string("Fehler beim Schreiben");
        }
        if (!e.finish(&err)) return err.empty() ? std::string("Fehler beim Abschließen") : err;
        return std::string();
    };
    if (std::string err = write(vocalsPath, vocals); !err.empty()) {
        result.ok = false;
        result.error = err;
        return result;
    }
    if (std::string err = write(instrumentalPath, inst); !err.empty()) {
        result.ok = false;
        result.error = err;
    }
    return result;
}

}  // namespace loggx
