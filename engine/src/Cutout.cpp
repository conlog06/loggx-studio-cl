// KI-Freistellung: U²-Net (Apache-2.0) schätzt pro Bild, was Vordergrund ist
// (Person/Objekt). Ergebnis ist eine Alpha-Maske in Bildgröße.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "loggx/engine/Engine.h"

#ifdef LOGGX_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace loggx {

namespace {
std::mutex g_cutoutMutex;
std::string g_cutoutPath;
#ifdef LOGGX_HAVE_ONNX
struct CutoutSession {
    std::unique_ptr<Ort::Session> session;
    std::string inName, outName;
    std::string path;
};
CutoutSession g_session;
Ort::Env& cutoutEnv() {
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "loggx-cutout");
    return env;
}
#endif
}  // namespace

void setCutoutModel(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_cutoutMutex);
    g_cutoutPath = path;
}

std::string cutoutModel() {
    std::lock_guard<std::mutex> lock(g_cutoutMutex);
    return g_cutoutPath;
}

bool cutoutMask(const std::uint8_t* rgba, int w, int h, std::vector<std::uint8_t>& alpha, std::string* error) {
#ifndef LOGGX_HAVE_ONNX
    (void)rgba; (void)w; (void)h; (void)alpha;
    if (error) *error = "Diese Version wurde ohne KI (ONNX Runtime) gebaut";
    return false;
#else
    constexpr int S = 320;
    // Eingabe: auf 320×320 verkleinern (Flächenmittel), durch das Maximum teilen,
    // dann ImageNet-Normierung – wie beim Training von U²-Net
    std::vector<float> input(3 * S * S);
    float maxV = 1e-6f;
    std::vector<float> small(3 * S * S);
    for (int y = 0; y < S; ++y) {
        const int ya = y * h / S, yb = std::max(ya + 1, (y + 1) * h / S);
        for (int x = 0; x < S; ++x) {
            const int xa = x * w / S, xb = std::max(xa + 1, (x + 1) * w / S);
            float sum[3] = {0, 0, 0};
            int n = 0;
            for (int yy = ya; yy < yb; yy += std::max(1, (yb - ya) / 4))
                for (int xx = xa; xx < xb; xx += std::max(1, (xb - xa) / 4)) {
                    const std::uint8_t* p = rgba + (static_cast<std::size_t>(yy) * w + xx) * 4;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    ++n;
                }
            for (int c = 0; c < 3; ++c) {
                const float v = sum[c] / static_cast<float>(n);
                small[static_cast<std::size_t>(c) * S * S + y * S + x] = v;
                maxV = std::max(maxV, v);
            }
        }
    }
    static const float mean[3] = {0.485f, 0.456f, 0.406f}, stdv[3] = {0.229f, 0.224f, 0.225f};
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < S * S; ++i)
            input[static_cast<std::size_t>(c) * S * S + i] = (small[static_cast<std::size_t>(c) * S * S + i] / maxV - mean[c]) / stdv[c];
    std::vector<float> out;
    try {
        std::lock_guard<std::mutex> lock(g_cutoutMutex);
        if (g_cutoutPath.empty()) {
            if (error) *error = "KI-Modell für die Freistellung fehlt";
            return false;
        }
        if (!g_session.session || g_session.path != g_cutoutPath) {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
            const std::wstring wpath = std::filesystem::path(std::u8string(g_cutoutPath.begin(), g_cutoutPath.end())).wstring();
            g_session.session = std::make_unique<Ort::Session>(cutoutEnv(), wpath.c_str(), options);
#else
            g_session.session = std::make_unique<Ort::Session>(cutoutEnv(), g_cutoutPath.c_str(), options);
#endif
            Ort::AllocatorWithDefaultOptions alloc;
            g_session.inName = g_session.session->GetInputNameAllocated(0, alloc).get();
            g_session.outName = g_session.session->GetOutputNameAllocated(0, alloc).get();
            g_session.path = g_cutoutPath;
        }
        const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::int64_t shape[4] = {1, 3, S, S};
        Ort::Value in = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape, 4);
        const char* inNames[] = {g_session.inName.c_str()};
        const char* outNames[] = {g_session.outName.c_str()};
        auto res = g_session.session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
        const float* o = res[0].GetTensorData<float>();
        out.assign(o, o + S * S);
    } catch (const Ort::Exception& e) {
        if (error) *error = e.what();
        g_session.session.reset();
        return false;
    }
    // Auf 0..1 strecken (wie in der Referenz) und bilinear auf Bildgröße bringen.
    // Findet das Modell gar kein Motiv (Höchstwert sehr klein), bleibt das Bild
    // sichtbar – sonst würde Rauschen zur Maske aufgeblasen.
    const auto [mn, mx] = std::minmax_element(out.begin(), out.end());
    if (*mx < 0.05f) {
        alpha.assign(static_cast<std::size_t>(w) * h, 255);
        return true;
    }
    const float lo = *mn, range = std::max(1e-6f, *mx - *mn);
    alpha.resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const float fy = std::clamp((y + 0.5f) * S / h - 0.5f, 0.0f, S - 1.0f);
        const int y0 = static_cast<int>(fy), y1 = std::min(S - 1, y0 + 1);
        const float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            const float fx = std::clamp((x + 0.5f) * S / w - 0.5f, 0.0f, S - 1.0f);
            const int x0 = static_cast<int>(fx), x1 = std::min(S - 1, x0 + 1);
            const float tx = fx - x0;
            const float v = (out[y0 * S + x0] * (1 - tx) + out[y0 * S + x1] * tx) * (1 - ty) +
                            (out[y1 * S + x0] * (1 - tx) + out[y1 * S + x1] * tx) * ty;
            alpha[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>(std::clamp((v - lo) / range, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    return true;
#endif
}

}  // namespace loggx
