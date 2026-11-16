// Eingebaute Stem-Engine ohne KI (sofort, ohne Download):
// 1. Gesang über die vorhandene Mitte/Stereo-Trennung
// 2. Rest per HPSS (Median-Filter über Zeit bzw. Frequenz) in
//    Schlagzeug (perkussiv) und tonal; tonal unten = Bass, oben = Instrumente
// Die vier Stems ergeben zusammen wieder das Original.
#include <algorithm>
#include <cmath>
#include <complex>

#include "../../third_party/pocketfft/pocketfft_hdronly.h"
#include "Parallel.h"
#include "loggx/engine/Engine.h"

namespace loggx {

namespace {

constexpr int kN = 4096;
constexpr int kHop = 1024;
constexpr int kMed = 17;   // Median-Länge (Frames bzw. Bins)

bool writeWav(const std::string& path, const std::vector<float>& data, int rate, std::string& error) {
    ExportSettings s;
    s.path = path;
    s.video = false;
    s.audioBits = 24;
    s.sampleRate = rate;
    Exporter e;
    if (!e.open(s, &error)) return false;
    for (std::size_t i = 0; i < data.size() / 2; i += 4096) {
        const std::size_t count = std::min<std::size_t>(4096, data.size() / 2 - i);
        if (!e.writeAudio(data.data() + i * 2, count)) {
            error = "Fehler beim Schreiben";
            return false;
        }
    }
    return e.finish(&error);
}

}  // namespace

bool splitHarmonicPercussive(const std::vector<float>& stereo, int rate, std::vector<float>& drums, std::vector<float>& bass,
                             const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    const std::size_t frames = stereo.size() / 2;
    drums.assign(stereo.size(), 0.0f);
    bass.assign(stereo.size(), 0.0f);
    if (frames < static_cast<std::size_t>(kN)) return true;
    const int bins = kN / 2 + 1;
    const int count = static_cast<int>((frames + kHop - 1) / kHop) + 1;
    std::vector<float> win(kN);
    for (int i = 0; i < kN; ++i) win[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / kN));
    const pocketfft::shape_t shape = {static_cast<std::size_t>(kN)};
    const pocketfft::stride_t rs = {sizeof(float)}, cs = {sizeof(std::complex<float>)};
    auto sampleAt = [&](long long i, int c) -> float {
        return i >= 0 && i < static_cast<long long>(frames) ? stereo[static_cast<std::size_t>(i) * 2 + c] : 0.0f;
    };
    // 1) Betragsspektrum der Mitte
    std::vector<float> mag(static_cast<std::size_t>(count) * bins);
    parallelRows(0, count, [&](int a, int b) {
        std::vector<float> buf(kN);
        std::vector<std::complex<float>> spec(static_cast<std::size_t>(bins));
        for (int f = a; f < b; ++f) {
            const long long start = static_cast<long long>(f) * kHop - kN / 2;
            for (int i = 0; i < kN; ++i)
                buf[static_cast<std::size_t>(i)] = 0.5f * (sampleAt(start + i, 0) + sampleAt(start + i, 1)) * win[static_cast<std::size_t>(i)];
            pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, buf.data(), spec.data(), 1.0f);
            for (int k = 0; k < bins; ++k) mag[static_cast<std::size_t>(f) * bins + k] = std::abs(spec[static_cast<std::size_t>(k)]);
        }
    }, 16);
    if (progress) progress(0.2);
    if (cancel && cancel->load()) return false;
    // 2) Median über Zeit (tonal) und Frequenz (perkussiv) → weiche Masken
    std::vector<float> maskP(mag.size()), maskB(mag.size());
    const double binHz = static_cast<double>(rate) / kN;
    parallelRows(0, count, [&](int a, int b) {
        float tmp[kMed];
        for (int f = a; f < b; ++f) {
            for (int k = 0; k < bins; ++k) {
                int n = 0;
                for (int d = -kMed / 2; d <= kMed / 2; ++d) {
                    const int ff = std::clamp(f + d, 0, count - 1);
                    tmp[n++] = mag[static_cast<std::size_t>(ff) * bins + k];
                }
                std::nth_element(tmp, tmp + n / 2, tmp + n);
                const float h = tmp[n / 2];
                n = 0;
                for (int d = -kMed / 2; d <= kMed / 2; ++d) {
                    const int kk = std::clamp(k + d, 0, bins - 1);
                    tmp[n++] = mag[static_cast<std::size_t>(f) * bins + kk];
                }
                std::nth_element(tmp, tmp + n / 2, tmp + n);
                const float p = tmp[n / 2];
                const float h2 = h * h, p2 = p * p;
                const float mp = h2 + p2 > 1e-20f ? p2 / (h2 + p2) : 0.5f;
                // Bass: tonaler Anteil unter ~120 Hz, weich bis 250 Hz
                const double hz = k * binHz;
                const float low = hz <= 120 ? 1.0f : hz >= 250 ? 0.0f : static_cast<float>(0.5 + 0.5 * std::cos(3.14159265358979 * (hz - 120) / 130));
                maskP[static_cast<std::size_t>(f) * bins + k] = mp;
                maskB[static_cast<std::size_t>(f) * bins + k] = (1.0f - mp) * low;
            }
        }
    }, 16);
    if (progress) progress(0.5);
    if (cancel && cancel->load()) return false;
    // 3) Masken je Kanal anwenden, zurück in die Zeit (Overlap-Add)
    // Hann² mit Hop N/4 summiert sich zu 1,5
    const float norm = 1.0f / (1.5f * kN);
    for (int c = 0; c < 2; ++c) {
        std::vector<float> outD(frames + kN, 0.0f), outB(frames + kN, 0.0f);
        // Frames in Blöcken parallel berechnen, Addition danach der Reihe nach
        const int block = 256;
        for (int f0 = 0; f0 < count; f0 += block) {
            const int f1 = std::min(count, f0 + block);
            std::vector<float> resD(static_cast<std::size_t>(f1 - f0) * kN), resB(resD.size());
            parallelRows(f0, f1, [&](int a, int b) {
                std::vector<float> buf(kN);
                std::vector<std::complex<float>> spec(static_cast<std::size_t>(bins)), tmpSpec(static_cast<std::size_t>(bins));
                for (int f = a; f < b; ++f) {
                    const long long start = static_cast<long long>(f) * kHop - kN / 2;
                    for (int i = 0; i < kN; ++i) buf[static_cast<std::size_t>(i)] = sampleAt(start + i, c) * win[static_cast<std::size_t>(i)];
                    pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, buf.data(), spec.data(), 1.0f);
                    for (int which = 0; which < 2; ++which) {
                        const std::vector<float>& mask = which == 0 ? maskP : maskB;
                        for (int k = 0; k < bins; ++k) tmpSpec[static_cast<std::size_t>(k)] = spec[static_cast<std::size_t>(k)] * mask[static_cast<std::size_t>(f) * bins + k];
                        pocketfft::c2r(shape, cs, rs, 0, pocketfft::BACKWARD, tmpSpec.data(), buf.data(), 1.0f);
                        float* dst = (which == 0 ? resD : resB).data() + static_cast<std::size_t>(f - f0) * kN;
                        for (int i = 0; i < kN; ++i) dst[i] = buf[static_cast<std::size_t>(i)] * win[static_cast<std::size_t>(i)] * norm;
                    }
                }
            }, 8);
            for (int f = f0; f < f1; ++f) {
                const long long start = static_cast<long long>(f) * kHop - kN / 2;
                for (int i = 0; i < kN; ++i) {
                    const long long t = start + i;
                    if (t < 0 || t >= static_cast<long long>(frames)) continue;
                    outD[static_cast<std::size_t>(t)] += resD[static_cast<std::size_t>(f - f0) * kN + i];
                    outB[static_cast<std::size_t>(t)] += resB[static_cast<std::size_t>(f - f0) * kN + i];
                }
            }
            if (cancel && cancel->load()) return false;
            if (progress) progress(0.5 + 0.25 * (c + static_cast<double>(f1) / count));
        }
        for (std::size_t i = 0; i < frames; ++i) {
            drums[i * 2 + c] = outD[i];
            bass[i * 2 + c] = outB[i];
        }
    }
    return true;
}

SeparationResult separateStemsDsp(const std::string& input, const std::string& vocalsPath, const std::string& drumsPath,
                                  const std::string& bassPath, const std::string& instPath,
                                  const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    SeparationResult result;
    constexpr int rate = 44100;
    AudioReader reader;
    std::string error;
    if (!reader.open(input, rate, &error)) {
        result.error = error.empty() ? "Datei nicht lesbar" : error;
        return result;
    }
    std::vector<float> stereo, buf(8192 * 2);
    for (;;) {
        const std::size_t got = reader.read(buf.data(), 8192);
        stereo.insert(stereo.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * 2));
        if (got < 8192) break;
        if (stereo.size() / 2 > static_cast<std::size_t>(rate) * 60 * 30) {
            result.error = "Länger als 30 Minuten";
            return result;
        }
    }
    std::vector<float> vocals, inst;
    result = separateVocalsBuffer(stereo, rate, vocals, inst, [&](double p) { if (progress) progress(p * 0.3); }, cancel);
    if (!result.ok) {
        // Mono: kein Gesang trennbar – alles bleibt im Instrumental
        if (!result.mono) return result;
        vocals.assign(stereo.size(), 0.0f);
        inst = stereo;
        result.ok = true;
    }
    std::vector<float> drums, bass;
    if (!splitHarmonicPercussive(inst, rate, drums, bass, [&](double p) { if (progress) progress(0.3 + p * 0.6); }, cancel)) {
        result.ok = false;
        result.error = "Abgebrochen";
        return result;
    }
    // Rest = Instrumental − Schlagzeug − Bass (Summe bleibt exakt)
    for (std::size_t i = 0; i < inst.size(); ++i) inst[i] -= drums[i] + bass[i];
    for (const auto& [path, data] : {std::pair<const std::string&, const std::vector<float>&>{vocalsPath, vocals}, {drumsPath, drums}, {bassPath, bass}, {instPath, inst}}) {
        if (!writeWav(path, data, rate, error)) {
            result.ok = false;
            result.error = error.empty() ? "Datei kann nicht geschrieben werden" : error;
            return result;
        }
    }
    if (progress) progress(1.0);
    result.ok = true;
    return result;
}

}  // namespace loggx
