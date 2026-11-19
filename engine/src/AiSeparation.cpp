// KI-Stemtrennung mit MDX-Net-Modellen (ONNX Runtime).
//
// Ablauf wie in der Referenz (Ultimate Vocal Remover, kuielab/mdx-net):
// Audio (44,1 kHz Stereo) in Abschnitte von hop·(dim_t−1) Samples teilen,
// je Abschnitt STFT (n_fft, Hop 1024, Hann, gespiegelter Rand) → Tensor
// [1, 4, dim_f, dim_t] (L re, L im, R re, R im), Modell → Spektrum des Stems,
// inverse STFT. Die Ränder jedes Abschnitts (n_fft/2) werden verworfen.
#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <thread>

#ifdef LOGGX_HAVE_ONNX
#include <onnxruntime_cxx_api.h>

#include "../../third_party/pocketfft/pocketfft_hdronly.h"
#endif

namespace loggx {

const std::vector<StemModelInfo>& stemModels() {
    static const std::string base = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/";
    static const std::string rembg = "https://github.com/danielgatis/rembg/releases/download/v0.0.0/";
    static const std::vector<StemModelInfo> models = {
        {"vocals-hq", "Gesang – beste Qualität (Kim Vocal 2)", "Kim_Vocal_2.onnx", base + "Kim_Vocal_2.onnx",
         "ce74ef3b6a6024ce44211a07be9cf8bc6d87728cc852a68ab34eb8e58cde9c8b", "vocals", 66759214, 7680, 3072, 8, 1.009},
        {"vocals", "Gesang – schnell (MDX-Net)", "kuielab_a_vocals.onnx", base + "kuielab_a_vocals.onnx",
         "daba83c2ee1afee9139766ad64c9b6808d6b6f092fff04bed3338be50baac721", "vocals", 29703204, 6144, 2048, 9, 1.035},
        {"drums", "Schlagzeug (MDX-Net)", "kuielab_a_drums.onnx", base + "kuielab_a_drums.onnx",
         "40f586b7091934dd6f5563f0cba8f14bad57ce88440da1098bf388ea716c2901", "drums", 29703204, 4096, 2048, 9, 1.035},
        {"bass", "Bass (MDX-Net)", "kuielab_a_bass.onnx", base + "kuielab_a_bass.onnx",
         "0c3e77b9963185b1ea6bb46a4b8924137d9370fc1ccdefec7b1b416ef550dcaa", "bass", 29703204, 16384, 2048, 9, 1.035},
        // KI-Freistellung (Video): U²-Net, Apache-2.0
        {"cutout", "Freistellung – schnell (U²-Net small)", "u2netp.onnx", rembg + "u2netp.onnx",
         "309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8", "cutout", 4574861, 0, 0, 0, 1.0},
        {"cutout-human", "Freistellung – Personen, beste Qualität (U²-Net)", "u2net_human_seg.onnx", rembg + "u2net_human_seg.onnx",
         "01eb6a29a5c4d8edb30b56adad9bb3a2a0535338e480724a213e0acfd2d1c73c", "cutout", 175997641, 0, 0, 0, 1.0},
    };
    return models;
}

const StemModelInfo* findStemModel(const std::string& id) {
    for (const auto& m : stemModels())
        if (m.id == id) return &m;
    return nullptr;
}

#ifndef LOGGX_HAVE_ONNX

bool aiSeparationAvailable() { return false; }

bool runStemModel(const StemModelInfo&, const std::string&, const std::vector<float>&, std::vector<float>&, bool,
                  const std::function<void(double)>&, const std::atomic<bool>*, std::string* error) {
    if (error) *error = "Diese Version wurde ohne KI (ONNX Runtime) gebaut";
    return false;
}

#else

bool aiSeparationAvailable() { return true; }

namespace {

Ort::Env& ortEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "loggx");
    return env;
}

}  // namespace

bool runStemModel(const StemModelInfo& info, const std::string& modelPath, const std::vector<float>& stereo,
                  std::vector<float>& stem, bool denoise, const std::function<void(double)>& progress,
                  const std::atomic<bool>* cancel, std::string* error) {
    const std::size_t n = stereo.size() / 2;
    stem.assign(stereo.size(), 0.0f);
    if (n == 0) return true;
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        // UTF-8 → UTF-16 (Pfade mit Umlauten)
        const std::wstring wpath = std::filesystem::path(std::u8string(modelPath.begin(), modelPath.end())).wstring();
        Ort::Session session(ortEnv(), wpath.c_str(), options);
#else
        Ort::Session session(ortEnv(), modelPath.c_str(), options);
#endif
        Ort::AllocatorWithDefaultOptions alloc;
        const std::string inName = session.GetInputNameAllocated(0, alloc).get();
        const std::string outName = session.GetOutputNameAllocated(0, alloc).get();
        const char* inNames[] = {inName.c_str()};
        const char* outNames[] = {outName.c_str()};

        const int nFft = info.nFft, dimF = info.dimF, dimT = 1 << info.dimTPow, hop = 1024;
        const int bins = nFft / 2 + 1;
        const std::size_t chunk = static_cast<std::size_t>(hop) * (dimT - 1);
        const std::size_t trim = static_cast<std::size_t>(nFft / 2);
        const std::size_t gen = chunk - 2 * trim;
        const std::size_t pad = gen - n % gen;
        // Gepolstertes Signal: trim Nullen | Audio | pad + trim Nullen (je Kanal)
        const std::size_t total = trim + n + pad + trim;
        std::vector<float> mix[2];
        for (int c = 0; c < 2; ++c) {
            mix[c].assign(total, 0.0f);
            for (std::size_t i = 0; i < n; ++i) mix[c][trim + i] = stereo[i * 2 + c];
        }

        std::vector<float> window(static_cast<std::size_t>(nFft));
        for (int i = 0; i < nFft; ++i)
            window[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / nFft));

        const std::size_t plane = static_cast<std::size_t>(dimF) * dimT;
        std::vector<float> input(4 * plane), negInput;
        const std::array<std::int64_t, 4> shape = {1, 4, dimF, dimT};
        const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        const pocketfft::shape_t fftShape = {static_cast<std::size_t>(nFft)};
        const pocketfft::stride_t rStride = {sizeof(float)};
        const pocketfft::stride_t cStride = {sizeof(std::complex<float>)};
        std::vector<float> frame(static_cast<std::size_t>(nFft));
        std::vector<std::complex<float>> spec(static_cast<std::size_t>(bins));
        // gespiegelt aufgefüllter Abschnitt (reflect, wie torch.stft center=True)
        std::vector<float> padded(chunk + static_cast<std::size_t>(nFft));
        const std::size_t half = static_cast<std::size_t>(nFft / 2);
        std::vector<float> ola(chunk + static_cast<std::size_t>(nFft)), norm(chunk + static_cast<std::size_t>(nFft));

        const std::size_t steps = (n + pad) / gen;
        for (std::size_t s = 0; s < steps; ++s) {
            if (cancel && cancel->load()) {
                if (error) *error = "Abgebrochen";
                return false;
            }
            const std::size_t start = s * gen;
            // 1) STFT → Tensor
            for (int c = 0; c < 2; ++c) {
                const float* x = mix[c].data() + start;
                for (std::size_t i = 0; i < padded.size(); ++i) {
                    const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(half);
                    std::ptrdiff_t k = j < 0 ? -j : j;
                    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(chunk) - 1;
                    if (k > last) k = 2 * last - k;
                    padded[i] = x[k];
                }
                for (int t = 0; t < dimT; ++t) {
                    const float* src = padded.data() + static_cast<std::size_t>(t) * hop;
                    for (int i = 0; i < nFft; ++i) frame[static_cast<std::size_t>(i)] = src[i] * window[static_cast<std::size_t>(i)];
                    pocketfft::r2c(fftShape, rStride, cStride, 0, pocketfft::FORWARD, frame.data(), spec.data(), 1.0f);
                    float* re = input.data() + static_cast<std::size_t>(c * 2) * plane;
                    float* im = re + plane;
                    for (int f = 0; f < dimF; ++f) {
                        const bool zero = f < 3;   // tiefste Bins wie in der Referenz leeren
                        re[static_cast<std::size_t>(f) * dimT + t] = zero ? 0.0f : spec[static_cast<std::size_t>(f)].real();
                        im[static_cast<std::size_t>(f) * dimT + t] = zero ? 0.0f : spec[static_cast<std::size_t>(f)].imag();
                    }
                }
            }
            // 2) Modell (optional zusätzlich mit umgekehrter Phase: weniger Rauschen)
            Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());
            auto out = session.Run(Ort::RunOptions{nullptr}, inNames, &inTensor, 1, outNames, 1);
            std::vector<float> pred(out[0].GetTensorData<float>(), out[0].GetTensorData<float>() + input.size());
            if (denoise) {
                negInput.resize(input.size());
                for (std::size_t i = 0; i < input.size(); ++i) negInput[i] = -input[i];
                Ort::Value negTensor = Ort::Value::CreateTensor<float>(mem, negInput.data(), negInput.size(), shape.data(), shape.size());
                auto negOut = session.Run(Ort::RunOptions{nullptr}, inNames, &negTensor, 1, outNames, 1);
                const float* q = negOut[0].GetTensorData<float>();
                for (std::size_t i = 0; i < pred.size(); ++i) pred[i] = 0.5f * pred[i] - 0.5f * q[i];
            }
            // 3) inverse STFT, Mitte des Abschnitts übernehmen
            for (int c = 0; c < 2; ++c) {
                std::fill(ola.begin(), ola.end(), 0.0f);
                std::fill(norm.begin(), norm.end(), 0.0f);
                const float* re = pred.data() + static_cast<std::size_t>(c * 2) * plane;
                const float* im = re + plane;
                for (int t = 0; t < dimT; ++t) {
                    for (int f = 0; f < bins; ++f)
                        spec[static_cast<std::size_t>(f)] = f < dimF ? std::complex<float>(re[static_cast<std::size_t>(f) * dimT + t],
                                                                                            im[static_cast<std::size_t>(f) * dimT + t])
                                                                     : std::complex<float>(0.0f, 0.0f);
                    pocketfft::c2r(fftShape, cStride, rStride, 0, pocketfft::BACKWARD, spec.data(), frame.data(), 1.0f / nFft);
                    float* dst = ola.data() + static_cast<std::size_t>(t) * hop;
                    float* nm = norm.data() + static_cast<std::size_t>(t) * hop;
                    for (int i = 0; i < nFft; ++i) {
                        const float w = window[static_cast<std::size_t>(i)];
                        dst[i] += frame[static_cast<std::size_t>(i)] * w;
                        nm[i] += w * w;
                    }
                }
                // Abschnitt [half, half+chunk) – davon nur die Mitte ohne trim-Ränder
                for (std::size_t i = trim; i < chunk - trim; ++i) {
                    const std::size_t pos = start + i - trim;   // Position im Original
                    if (pos >= n) break;
                    const float d = norm[half + i];
                    stem[pos * 2 + c] = d > 1e-8f ? ola[half + i] / d * static_cast<float>(info.compensate) : 0.0f;
                }
            }
            if (progress) progress(static_cast<double>(s + 1) / steps);
        }
    } catch (const Ort::Exception& e) {
        if (error) *error = std::string("KI-Fehler: ") + e.what();
        return false;
    }
    return true;
}

#endif

namespace {

bool loadStereo(const std::string& path, int rate, std::vector<float>& out, std::string* error) {
    AudioReader reader;
    if (!reader.open(path, rate, error)) return false;
    const std::size_t maxFrames = static_cast<std::size_t>(rate) * 60 * 30;
    std::vector<float> buf(8192 * 2);
    out.clear();
    for (;;) {
        const std::size_t got = reader.read(buf.data(), 8192);
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * 2));
        if (got < 8192) break;
        if (out.size() / 2 > maxFrames) {
            if (error) *error = "Länger als 30 Minuten – bitte zuerst kürzen";
            return false;
        }
    }
    return true;
}

bool writeWav(const std::string& path, const std::vector<float>& data, int rate, std::string* error) {
    ExportSettings s;
    s.path = path;
    s.video = false;
    s.audioBits = 24;
    s.sampleRate = rate;
    Exporter e;
    if (!e.open(s, error)) return false;
    for (std::size_t i = 0; i < data.size() / 2; i += 4096)
        if (!e.writeAudio(data.data() + i * 2, std::min<std::size_t>(4096, data.size() / 2 - i))) {
            if (error) *error = "Fehler beim Schreiben";
            return false;
        }
    return e.finish(error);
}

}  // namespace

SeparationResult separateStemsAi(const std::string& input, const std::vector<StemJob>& jobs, const std::string& restPath,
                                 bool denoise, const std::function<void(double)>& progress, const std::atomic<bool>* cancel) {
    SeparationResult result;
    constexpr int kRate = 44100;   // MDX-Net ist auf 44,1 kHz trainiert
    std::vector<float> mix;
    if (!loadStereo(input, kRate, mix, &result.error)) return result;
    if (mix.empty()) {
        result.error = "Keine Tonspur";
        return result;
    }
    std::vector<float> rest = mix;
    double eStem = 0, eAll = 0;
    for (float v : mix) eAll += static_cast<double>(v) * v;
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        std::vector<float> stem;
        const auto part = [&](double p) {
            if (progress) progress((static_cast<double>(j) + p) / static_cast<double>(jobs.size()));
        };
        if (!runStemModel(*jobs[j].model, jobs[j].modelPath, mix, stem, denoise, part, cancel, &result.error)) return result;
        for (std::size_t i = 0; i < stem.size(); ++i) {
            rest[i] -= stem[i];
            if (j == 0) eStem += static_cast<double>(stem[i]) * stem[i];
        }
        if (!writeWav(jobs[j].outputPath, stem, kRate, &result.error)) return result;
    }
    if (!restPath.empty() && !writeWav(restPath, rest, kRate, &result.error)) return result;
    result.vocalShare = eAll > 0 ? eStem / eAll : 0.0;
    result.ok = true;
    return result;
}

}  // namespace loggx
