// KI-Stemtrennung mit HTDemucs v4 (Meta, MIT-Lizenz) über ONNX Runtime.
//
// Das ONNX-Modell (umgewandelt mit sevagh/demucs.onnx) enthält das neuronale
// Netz ohne STFT/iSTFT. Beides wird hier exakt wie in PyTorch nachgebaut:
//   _spec:  Rand 1536 gespiegelt (+ Auffüllen auf volle Hops), torch.stft mit
//           n_fft 4096, Hop 1024, periodischem Hann, normalized, center/reflect;
//           letzte Frequenz weg, Frames 2 … 2+le.
//   _ispec: Frequenz und je 2 Frames Rand mit Nullen, torch.istft, Ausschnitt.
// Ergebnis je Quelle = iSTFT(Spektral-Zweig) + Zeit-Zweig.
// Segmente wie apply_model(split=True, overlap=0.25): 7,8 s, Dreiecks-Gewichte,
// Normalisierung der Mischung über Mittelwert/Standardabweichung (Mono).
#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <thread>

#ifdef LOGGX_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

#include "../../third_party/pocketfft/pocketfft_hdronly.h"

namespace loggx {

namespace demucs {

constexpr int kFft = 4096, kHop = 1024, kBins = 2048;
constexpr std::size_t kSegment = 343980;   // 7,8 s bei 44,1 kHz
constexpr int kFrames = 336;               // ceil(kSegment / kHop)
constexpr int kPad = kHop / 2 * 3;         // 1536

namespace {

const std::vector<float>& hann() {
    static const std::vector<float> w = [] {
        std::vector<float> v(kFft);
        for (int i = 0; i < kFft; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * i / kFft));
        return v;
    }();
    return w;
}

// numpy/torch „reflect“: Rand wird nicht wiederholt
inline float reflectAt(const float* x, long long n, long long i) {
    if (n == 1) return x[0];
    const long long period = 2 * (n - 1);
    i %= period;
    if (i < 0) i += period;
    return x[i < n ? i : period - i];
}

}  // namespace

// Spektrogramm wie HTDemucs._spec für ein Segment (kSegment Samples, ein Kanal).
// spec: kBins × kFrames komplex (Zeilen = Frequenz).
void spec(const float* x, std::size_t n, std::vector<std::complex<float>>& out) {
    const long long le = static_cast<long long>((n + kHop - 1) / kHop);
    const long long padR = kPad + le * kHop - static_cast<long long>(n);
    const long long pn = kPad + static_cast<long long>(n) + padR;   // Länge nach _spec-Rand
    // Gesamtsignal mit center-Rand (n_fft/2) beidseitig, beide Male gespiegelt
    std::vector<float> p(static_cast<std::size_t>(pn));
    for (long long i = 0; i < pn; ++i) p[static_cast<std::size_t>(i)] = reflectAt(x, static_cast<long long>(n), i - kPad);
    const long long half = kFft / 2;
    const long long total = pn + 2 * half;
    std::vector<float> y(static_cast<std::size_t>(total));
    for (long long i = 0; i < total; ++i) y[static_cast<std::size_t>(i)] = reflectAt(p.data(), pn, i - half);
    const auto& w = hann();
    const pocketfft::shape_t shape = {static_cast<std::size_t>(kFft)};
    const pocketfft::stride_t rs = {sizeof(float)}, cs = {sizeof(std::complex<float>)};
    std::vector<float> frame(kFft);
    std::vector<std::complex<float>> f(kFft / 2 + 1);
    out.assign(static_cast<std::size_t>(kBins) * static_cast<std::size_t>(le), {});
    const float scale = 1.0f / std::sqrt(static_cast<float>(kFft));
    for (long long t = 0; t < le; ++t) {
        const long long j = t + 2;   // Frames 2 … 2+le
        for (int i = 0; i < kFft; ++i) frame[static_cast<std::size_t>(i)] = y[static_cast<std::size_t>(j * kHop + i)] * w[static_cast<std::size_t>(i)];
        pocketfft::r2c(shape, rs, cs, 0, pocketfft::FORWARD, frame.data(), f.data(), scale);
        for (int b = 0; b < kBins; ++b) out[static_cast<std::size_t>(b) * static_cast<std::size_t>(le) + static_cast<std::size_t>(t)] = f[static_cast<std::size_t>(b)];
    }
}

// Inverse wie HTDemucs._ispec: z = kBins × le, Ergebnis n Samples.
void ispec(const std::vector<std::complex<float>>& z, int le, std::size_t n, std::vector<float>& out) {
    const int frames = le + 4;   // je 2 Null-Frames links/rechts
    const long long olaLen = kFft + static_cast<long long>(kHop) * (frames - 1);
    std::vector<float> ola(static_cast<std::size_t>(olaLen), 0.0f), env(static_cast<std::size_t>(olaLen), 0.0f);
    const auto& w = hann();
    const pocketfft::shape_t shape = {static_cast<std::size_t>(kFft)};
    const pocketfft::stride_t rs = {sizeof(float)}, cs = {sizeof(std::complex<float>)};
    std::vector<std::complex<float>> f(kFft / 2 + 1);
    std::vector<float> frame(kFft);
    const float scale = 1.0f / std::sqrt(static_cast<float>(kFft));   // normalized: ·√N, irfft: /N
    for (int j = 0; j < frames; ++j) {
        const int t = j - 2;
        const bool zero = t < 0 || t >= le;
        if (!zero) {
            for (int b = 0; b < kBins; ++b) f[static_cast<std::size_t>(b)] = z[static_cast<std::size_t>(b) * static_cast<std::size_t>(le) + static_cast<std::size_t>(t)];
            f[kBins] = {};
            pocketfft::c2r(shape, cs, rs, 0, pocketfft::BACKWARD, f.data(), frame.data(), scale);
        }
        for (int i = 0; i < kFft; ++i) {
            const std::size_t k = static_cast<std::size_t>(static_cast<long long>(j) * kHop + i);
            if (!zero) ola[k] += frame[static_cast<std::size_t>(i)] * w[static_cast<std::size_t>(i)];
            env[k] += w[static_cast<std::size_t>(i)] * w[static_cast<std::size_t>(i)];
        }
    }
    out.assign(n, 0.0f);
    const long long start = kFft / 2 + kPad;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t k = static_cast<std::size_t>(start + static_cast<long long>(i));
        out[i] = env[k] > 1e-11f ? ola[k] / env[k] : 0.0f;
    }
}

}  // namespace demucs

#ifndef LOGGX_HAVE_ONNX

bool runDemucs(const std::string&, const std::vector<float>&, std::vector<float> (&)[4], const std::function<void(double)>&,
               const std::atomic<bool>*, std::string* error) {
    if (error) *error = "Diese Version wurde ohne KI (ONNX Runtime) gebaut";
    return false;
}

#else

namespace {
Ort::Env& demucsEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "loggx-demucs");
    return env;
}
}  // namespace

bool runDemucs(const std::string& modelPath, const std::vector<float>& stereo, std::vector<float> (&sources)[4],
               const std::function<void(double)>& progress, const std::atomic<bool>* cancel, std::string* error) {
    using namespace demucs;
    const std::size_t n = stereo.size() / 2;
    for (auto& s : sources) s.assign(stereo.size(), 0.0f);
    if (n == 0) return true;
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        const std::wstring wpath = std::filesystem::path(std::u8string(modelPath.begin(), modelPath.end())).wstring();
        Ort::Session session(demucsEnv(), wpath.c_str(), options);
#else
        Ort::Session session(demucsEnv(), modelPath.c_str(), options);
#endif
        Ort::AllocatorWithDefaultOptions alloc;
        // Ein- und Ausgänge über ihren Rang erkennen (Spektrum 4/5, Wellenform 3/4)
        std::string inSpec, inWave, outSpec, outWave;
        for (std::size_t i = 0; i < session.GetInputCount(); ++i) {
            const std::string name = session.GetInputNameAllocated(i, alloc).get();
            const auto rank = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetDimensionsCount();
            (rank == 4 ? inSpec : inWave) = name;
        }
        for (std::size_t i = 0; i < session.GetOutputCount(); ++i) {
            const std::string name = session.GetOutputNameAllocated(i, alloc).get();
            const auto rank = session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetDimensionsCount();
            (rank == 5 ? outSpec : outWave) = name;
        }
        if (inSpec.empty() || inWave.empty() || outSpec.empty() || outWave.empty()) {
            if (error) *error = "Unerwartetes Demucs-Modell";
            return false;
        }
        const char* inNames[] = {inSpec.c_str(), inWave.c_str()};
        const char* outNames[] = {outSpec.c_str(), outWave.c_str()};
        const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Normalisierung über das Mono-Signal (wie demucs.separate)
        double mean = 0, var = 0;
        for (std::size_t i = 0; i < n; ++i) mean += 0.5 * (stereo[i * 2] + stereo[i * 2 + 1]);
        mean /= static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double d = 0.5 * (stereo[i * 2] + stereo[i * 2 + 1]) - mean;
            var += d * d;
        }
        const double stdv = std::sqrt(var / static_cast<double>(std::max<std::size_t>(1, n - 1)));
        const double inv = 1.0 / (stdv + 1e-8);
        std::vector<float> mix[2];
        for (int c = 0; c < 2; ++c) {
            mix[c].resize(n);
            for (std::size_t i = 0; i < n; ++i) mix[c][i] = static_cast<float>((stereo[i * 2 + c] - mean) * inv);
        }

        // Dreiecks-Gewichte über das Segment
        std::vector<float> weight(kSegment);
        {
            const std::size_t h = kSegment / 2;
            for (std::size_t i = 0; i < h; ++i) weight[i] = static_cast<float>(i + 1);
            for (std::size_t i = h; i < kSegment; ++i) weight[i] = static_cast<float>(kSegment - i);
            const float mx = *std::max_element(weight.begin(), weight.end());
            for (float& v : weight) v /= mx;
        }
        const std::size_t stride = static_cast<std::size_t>(0.75 * static_cast<double>(kSegment));
        std::vector<double> acc[4][2], sumW(n, 0.0);
        for (auto& s : acc)
            for (auto& c : s) c.assign(n, 0.0);
        const std::size_t steps = (n + stride - 1) / stride;

        std::vector<float> wave(2 * kSegment), specIn(4ull * kBins * kFrames);
        std::vector<std::complex<float>> z, zs(static_cast<std::size_t>(kBins) * kFrames);
        std::vector<float> chan, back;
        const std::array<std::int64_t, 4> specShape = {1, 4, kBins, kFrames};
        const std::array<std::int64_t, 3> waveShape = {1, 2, static_cast<std::int64_t>(kSegment)};
        std::size_t step = 0;
        for (std::size_t offset = 0; offset < n; offset += stride, ++step) {
            if (cancel && cancel->load()) {
                if (error) *error = "Abgebrochen";
                return false;
            }
            const std::size_t chunkLen = std::min(kSegment, n - offset);
            // TensorChunk.padded: symmetrisch mit echten Nachbar-Samples, sonst Nullen
            const long long delta = static_cast<long long>(kSegment) - static_cast<long long>(chunkLen);
            const long long start = static_cast<long long>(offset) - delta / 2;
            for (int c = 0; c < 2; ++c)
                for (std::size_t i = 0; i < kSegment; ++i) {
                    const long long k = start + static_cast<long long>(i);
                    wave[static_cast<std::size_t>(c) * kSegment + i] = k >= 0 && k < static_cast<long long>(n) ? mix[c][static_cast<std::size_t>(k)] : 0.0f;
                }
            for (int c = 0; c < 2; ++c) {
                spec(wave.data() + static_cast<std::size_t>(c) * kSegment, kSegment, z);
                for (std::size_t k = 0; k < z.size(); ++k) {
                    specIn[(static_cast<std::size_t>(c * 2) * kBins * kFrames) + k] = z[k].real();
                    specIn[(static_cast<std::size_t>(c * 2 + 1) * kBins * kFrames) + k] = z[k].imag();
                }
            }
            Ort::Value inputs[2] = {
                Ort::Value::CreateTensor<float>(mem, specIn.data(), specIn.size(), specShape.data(), specShape.size()),
                Ort::Value::CreateTensor<float>(mem, wave.data(), wave.size(), waveShape.data(), waveShape.size())};
            auto out = session.Run(Ort::RunOptions{nullptr}, inNames, inputs, 2, outNames, 2);
            const float* so = out[0].GetTensorData<float>();
            const float* wo = out[1].GetTensorData<float>();
            // Ausschnitt des echten Chunks im gepolsterten Segment (center_trim)
            const std::size_t trim = static_cast<std::size_t>(delta / 2);
            for (int s = 0; s < 4; ++s)
                for (int c = 0; c < 2; ++c) {
                    const std::size_t plane = static_cast<std::size_t>(kBins) * kFrames;
                    const float* re = so + (static_cast<std::size_t>(s * 4 + c * 2)) * plane;
                    const float* im = so + (static_cast<std::size_t>(s * 4 + c * 2 + 1)) * plane;
                    for (std::size_t k = 0; k < plane; ++k) zs[k] = {re[k], im[k]};
                    ispec(zs, kFrames, kSegment, back);
                    const float* xt = wo + (static_cast<std::size_t>(s * 2 + c)) * kSegment;
                    auto& a = acc[s][c];
                    for (std::size_t i = 0; i < chunkLen; ++i) {
                        const std::size_t src = trim + i;
                        a[offset + i] += static_cast<double>(weight[i]) * (back[src] + xt[src]);
                    }
                }
            for (std::size_t i = 0; i < chunkLen; ++i) sumW[offset + i] += weight[i];
            if (progress) progress(static_cast<double>(step + 1) / static_cast<double>(steps));
        }
        for (int s = 0; s < 4; ++s)
            for (int c = 0; c < 2; ++c)
                for (std::size_t i = 0; i < n; ++i)
                    sources[s][i * 2 + c] = static_cast<float>(acc[s][c][i] / std::max(1e-9, sumW[i]) * stdv + mean);
    } catch (const Ort::Exception& e) {
        if (error) *error = std::string("KI-Fehler: ") + e.what();
        return false;
    }
    return true;
}

#endif

}  // namespace loggx
