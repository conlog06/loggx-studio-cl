// Multicam: Kameras über den Ton synchronisieren (Kreuzkorrelation).
// Grob über die Lautstärke-Hüllkurve (100 Hz, per FFT über die ganze Länge),
// dann fein über das Signal selbst (8 kHz) um den Treffer herum.
#include <algorithm>
#include <cmath>
#include <complex>

#include "../../third_party/pocketfft/pocketfft_hdronly.h"
#include "loggx/engine/Engine.h"
#include "loggx/media/Media.h"

namespace loggx {

namespace {
constexpr int kRate = 8000;      // Feinabgleich
constexpr int kEnvRate = 100;    // Hüllkurve: 10 ms

bool loadMono(const std::string& path, TimeUs maxDur, std::vector<float>& out) {
    AudioReader r;
    if (!r.open(path, kRate)) return false;
    const std::size_t maxFrames = static_cast<std::size_t>(maxDur / 1'000'000.0 * kRate);
    std::vector<float> buf(8192 * 2);
    out.clear();
    while (out.size() < maxFrames) {
        const std::size_t n = r.read(buf.data(), 8192);
        for (std::size_t i = 0; i < n; ++i) out.push_back(0.5f * (buf[i * 2] + buf[i * 2 + 1]));
        if (n < 8192) break;
    }
    return out.size() > static_cast<std::size_t>(kRate);   // mindestens 1 s
}

// Hüllkurve: Anstieg der Lautheit (Einsätze), mittelwertfrei
std::vector<float> envelope(const std::vector<float>& x) {
    const std::size_t hop = kRate / kEnvRate;
    std::vector<float> e(x.size() / hop);
    for (std::size_t i = 0; i < e.size(); ++i) {
        double s = 0;
        for (std::size_t k = 0; k < hop; ++k) s += x[i * hop + k] * x[i * hop + k];
        e[i] = static_cast<float>(std::log1p(1000.0 * std::sqrt(s / hop)));
    }
    std::vector<float> d(e.size(), 0.0f);
    for (std::size_t i = 1; i < e.size(); ++i) d[i] = std::max(0.0f, e[i] - e[i - 1]);
    double mean = 0;
    for (float v : d) mean += v;
    mean /= std::max<std::size_t>(1, d.size());
    for (float& v : d) v -= static_cast<float>(mean);
    return d;
}

// Kreuzkorrelation per FFT; liefert Verschiebung (in Abtastwerten) von b gegenüber a
// und die normierte Güte des Treffers
std::pair<long, double> xcorr(const std::vector<float>& a, const std::vector<float>& b) {
    std::size_t n = 1;
    while (n < a.size() + b.size()) n <<= 1;
    std::vector<double> pa(n, 0.0), pb(n, 0.0);
    std::copy(a.begin(), a.end(), pa.begin());
    std::copy(b.begin(), b.end(), pb.begin());
    std::vector<std::complex<double>> fa(n / 2 + 1), fb(n / 2 + 1);
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t rs{sizeof(double)}, cs{sizeof(std::complex<double>)};
    pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, pa.data(), fa.data(), 1.0);
    pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, pb.data(), fb.data(), 1.0);
    for (std::size_t i = 0; i < fa.size(); ++i) fa[i] *= std::conj(fb[i]);
    std::vector<double> c(n);
    pocketfft::c2r(shape, cs, rs, 0, pocketfft::BACKWARD, fa.data(), c.data(), 1.0 / static_cast<double>(n));
    std::size_t best = 0;
    for (std::size_t i = 1; i < n; ++i)
        if (c[i] > c[best]) best = i;
    double na = 0, nb = 0;
    for (float v : a) na += v * v;
    for (float v : b) nb += v * v;
    const double q = c[best] / std::sqrt(std::max(1e-12, na * nb));
    const long lag = best < n / 2 ? static_cast<long>(best) : static_cast<long>(best) - static_cast<long>(n);
    return {lag, q};
}
}  // namespace

std::vector<AudioSyncResult> syncByAudio(const std::vector<std::string>& paths, TimeUs maxDuration,
                                         const std::function<bool(double)>& progress) {
    std::vector<AudioSyncResult> out(paths.size());
    if (paths.empty()) return out;
    std::vector<float> ref;
    if (!loadMono(paths[0], maxDuration, ref)) {
        for (auto& r : out) r.error = "Kein Ton in der ersten Kamera";
        return out;
    }
    out[0].ok = true;
    out[0].confidence = 1.0;
    const std::vector<float> refEnv = envelope(ref);
    for (std::size_t i = 1; i < paths.size(); ++i) {
        if (progress && !progress(static_cast<double>(i) / static_cast<double>(paths.size()))) break;
        std::vector<float> x;
        if (!loadMono(paths[i], maxDuration, x)) {
            out[i].error = "Kein Ton";
            continue;
        }
        const auto [coarse, q] = xcorr(refEnv, envelope(x));
        // Fein: ±30 ms um den Treffer im Signal selbst
        long bestLag = coarse * (kRate / kEnvRate);
        double bestScore = -1e300;
        const long win = kRate * 30 / 1000;
        const long len = std::min<long>(static_cast<long>(std::min(ref.size(), x.size())), kRate * 20);
        for (long lag = bestLag - win; lag <= bestLag + win; ++lag) {
            // ref[t] ≈ x[t − lag]; Ausschnitt dort, wo beide Ton haben
            const long t0 = std::max<long>(0, lag), t1 = std::min<long>(static_cast<long>(ref.size()), static_cast<long>(x.size()) + lag);
            if (t1 - t0 < kRate) continue;
            double s = 0;
            const long mid = (t0 + t1) / 2, a = std::max(t0, mid - len / 2), b = std::min(t1, a + len);
            for (long t = a; t < b; t += 2) s += ref[static_cast<std::size_t>(t)] * x[static_cast<std::size_t>(t - lag)];
            if (s > bestScore) bestScore = s, bestLag = lag;
        }
        // lag > 0: Kamera i startet später als Kamera 1
        out[i].offset = static_cast<TimeUs>(std::llround(bestLag * 1e6 / kRate));
        out[i].confidence = q;
        out[i].ok = q > 0.05;
        if (!out[i].ok) out[i].error = "Ton passt nicht zusammen";
    }
    return out;
}

}  // namespace loggx
