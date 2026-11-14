#include "ImageOps.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Parallel.h"
#include "loggx/engine/Engine.h"

namespace loggx {

// ------------------------------------------------------------------ LUT

std::shared_ptr<Lut3D> Lut3D::load(const std::string& path, std::string* error) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())));
    if (!f) {
        if (error) *error = "LUT-Datei kann nicht geöffnet werden";
        return nullptr;
    }
    auto lut = std::make_shared<Lut3D>();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream ss(line.substr(first));
        if (std::isalpha(static_cast<unsigned char>(line[first]))) {
            std::string key;
            ss >> key;
            if (key == "LUT_3D_SIZE") ss >> lut->size;
            else if (key == "DOMAIN_MIN") ss >> lut->domainMin[0] >> lut->domainMin[1] >> lut->domainMin[2];
            else if (key == "DOMAIN_MAX") ss >> lut->domainMax[0] >> lut->domainMax[1] >> lut->domainMax[2];
            else if (key == "LUT_1D_SIZE") {
                if (error) *error = "1D-LUTs werden nicht unterstützt";
                return nullptr;
            }
            continue;
        }
        float r, g, b;
        if (ss >> r >> g >> b) {
            lut->data.push_back(r);
            lut->data.push_back(g);
            lut->data.push_back(b);
        }
    }
    const std::size_t expected =
        static_cast<std::size_t>(lut->size) * lut->size * lut->size * 3;
    if (lut->size < 2 || lut->size > 256 || lut->data.size() != expected) {
        if (error) *error = "Ungültige oder unvollständige .cube-Datei";
        return nullptr;
    }
    return lut;
}

bool validateLut(const std::string& path, std::string* error) {
    return Lut3D::load(path, error) != nullptr;
}

void Lut3D::apply(float& r, float& g, float& b) const {
    const float in[3] = {r, g, b};
    float pos[3];
    int i0[3], i1[3];
    float frac[3];
    for (int c = 0; c < 3; ++c) {
        const float range = domainMax[c] - domainMin[c];
        float v = range > 0 ? (in[c] - domainMin[c]) / range : in[c];
        v = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(size - 1);
        pos[c] = v;
        i0[c] = std::min(static_cast<int>(v), size - 2);
        i1[c] = i0[c] + 1;
        frac[c] = v - static_cast<float>(i0[c]);
    }
    (void)pos;
    auto at = [this](int ri, int gi, int bi, int c) {
        return data[(static_cast<std::size_t>(bi) * size * size + static_cast<std::size_t>(gi) * size + ri) * 3 + c];
    };
    float out[3];
    for (int c = 0; c < 3; ++c) {
        const float c000 = at(i0[0], i0[1], i0[2], c), c100 = at(i1[0], i0[1], i0[2], c);
        const float c010 = at(i0[0], i1[1], i0[2], c), c110 = at(i1[0], i1[1], i0[2], c);
        const float c001 = at(i0[0], i0[1], i1[2], c), c101 = at(i1[0], i0[1], i1[2], c);
        const float c011 = at(i0[0], i1[1], i1[2], c), c111 = at(i1[0], i1[1], i1[2], c);
        const float c00 = c000 + (c100 - c000) * frac[0];
        const float c10 = c010 + (c110 - c010) * frac[0];
        const float c01 = c001 + (c101 - c001) * frac[0];
        const float c11 = c011 + (c111 - c011) * frac[0];
        const float c0 = c00 + (c10 - c00) * frac[1];
        const float c1 = c01 + (c11 - c01) * frac[1];
        out[c] = c0 + (c1 - c0) * frac[2];
    }
    r = out[0];
    g = out[1];
    b = out[2];
}

// --------------------------------------------------------- Farbkorrektur

void applyColorGrade(std::vector<std::uint8_t>& rgba, int w, int h, const ColorGrade& g,
                     const Lut3D* lut) {
    // Alles außer Sättigung und LUT wirkt pro Kanal und passt in Tabellen.
    std::array<std::array<float, 256>, 3> table;
    const double wb[3] = {1.0 + 0.12 * g.temperature, 1.0 - 0.10 * g.tint,
                          1.0 - 0.12 * g.temperature};
    const double expo = std::pow(2.0, g.exposure);
    const double invGamma = 1.0 / std::max(0.05, g.gamma);
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < 256; ++i) {
            double v = i / 255.0;
            v *= expo * wb[c];
            v = g.gain * (v + g.lift * (1.0 - v));
            v = std::pow(std::max(0.0, v), invGamma);
            v = (v - 0.5) * g.contrast + 0.5;
            table[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                static_cast<float>(std::clamp(v, 0.0, 1.0));
        }
    }
    const float sat = static_cast<float>(g.saturation);
    const float mix = lut ? static_cast<float>(std::clamp(g.lutMix, 0.0, 1.0)) : 0.0f;

    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                float r = table[0][p[0]], gg = table[1][p[1]], b = table[2][p[2]];
                if (sat != 1.0f) {
                    const float luma = 0.2126f * r + 0.7152f * gg + 0.0722f * b;
                    r = luma + (r - luma) * sat;
                    gg = luma + (gg - luma) * sat;
                    b = luma + (b - luma) * sat;
                }
                if (mix > 0.0f) {
                    float lr = std::clamp(r, 0.0f, 1.0f), lg = std::clamp(gg, 0.0f, 1.0f),
                          lb = std::clamp(b, 0.0f, 1.0f);
                    lut->apply(lr, lg, lb);
                    r += (lr - r) * mix;
                    gg += (lg - gg) * mix;
                    b += (lb - b) * mix;
                }
                p[0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
                p[1] = static_cast<std::uint8_t>(std::clamp(gg, 0.0f, 1.0f) * 255.0f + 0.5f);
                p[2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    });
}

// ------------------------------------------------------------ Chroma Key

void applyChromaKey(std::vector<std::uint8_t>& rgba, int w, int h, const Effects& fx) {
    // Abstand in der Farbebene (Cb/Cr), unabhängig von der Helligkeit.
    auto chroma = [](float r, float g, float b, float& cb, float& cr) {
        cb = -0.1146f * r - 0.3854f * g + 0.5f * b;
        cr = 0.5f * r - 0.4542f * g - 0.0458f * b;
    };
    float kcb, kcr;
    chroma(static_cast<float>(fx.keyR), static_cast<float>(fx.keyG), static_cast<float>(fx.keyB), kcb, kcr);
    const float tol = static_cast<float>(std::max(0.0, fx.keyTolerance)) * 0.5f;
    const float soft = static_cast<float>(std::max(0.005, fx.keySoftness)) * 0.5f;
    const bool greenKey = fx.keyG >= fx.keyR && fx.keyG >= fx.keyB;
    const bool blueKey = !greenKey && fx.keyB >= fx.keyR;

    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
                float cb, cr;
                chroma(r, g, b, cb, cr);
                const float d = std::sqrt((cb - kcb) * (cb - kcb) + (cr - kcr) * (cr - kcr));
                float a = std::clamp((d - tol) / soft, 0.0f, 1.0f);
                a = a * a * (3.0f - 2.0f * a);
                if (a < 1.0f) {
                    // Farbsaum der Schlüsselfarbe entfernen (Spill).
                    if (greenKey) p[1] = std::min(p[1], std::max(p[0], p[2]));
                    else if (blueKey) p[2] = std::min(p[2], std::max(p[0], p[1]));
                }
                p[3] = static_cast<std::uint8_t>(p[3] * a + 0.5f);
            }
        }
    });
}

// --------------------------------------------------------------- Vignette

void applyVignette(std::vector<std::uint8_t>& rgba, int w, int h, double amount) {
    const float k = static_cast<float>(std::clamp(amount, 0.0, 1.0));
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float ny = (y + 0.5f) / h * 2.0f - 1.0f;
            std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                const float nx = (x + 0.5f) / w * 2.0f - 1.0f;
                const float d = std::sqrt(nx * nx + ny * ny) / 1.4142f;
                float v = std::clamp((d - 0.35f) / 0.65f, 0.0f, 1.0f);
                v = v * v * (3.0f - 2.0f * v);
                const float f = 1.0f - k * v;
                p[0] = static_cast<std::uint8_t>(p[0] * f);
                p[1] = static_cast<std::uint8_t>(p[1] * f);
                p[2] = static_cast<std::uint8_t>(p[2] * f);
            }
        }
    });
}

// ----------------------------------------------------------------- Blur

namespace {

// Ein Box-Filter-Durchgang über eine Zeile oder Spalte (vormultipliziert).
void boxLine(const float* in, float* out, int n, int stride, int r) {
    const float norm = 1.0f / static_cast<float>(2 * r + 1);
    float sum[4] = {0, 0, 0, 0};
    auto px = [&](int i) { return in + static_cast<std::ptrdiff_t>(std::clamp(i, 0, n - 1)) * stride; };
    for (int i = -r; i <= r; ++i)
        for (int c = 0; c < 4; ++c) sum[c] += px(i)[c];
    for (int i = 0; i < n; ++i) {
        float* o = out + static_cast<std::ptrdiff_t>(i) * stride;
        for (int c = 0; c < 4; ++c) o[c] = sum[c] * norm;
        const float* add = px(i + r + 1);
        const float* sub = px(i - r);
        for (int c = 0; c < 4; ++c) sum[c] += add[c] - sub[c];
    }
}

}  // namespace

void applyBlur(std::vector<std::uint8_t>& rgba, int w, int h, double radius) {
    if (radius < 0.5 || w < 2 || h < 2) return;
    const int r = std::max(1, static_cast<int>(std::lround(radius * 0.58)));
    const std::size_t n = static_cast<std::size_t>(w) * h * 4;
    std::vector<float> a(n), b(n);
    for (std::size_t i = 0; i < n; i += 4) {
        const float alpha = rgba[i + 3] / 255.0f;
        a[i] = rgba[i] * alpha;
        a[i + 1] = rgba[i + 1] * alpha;
        a[i + 2] = rgba[i + 2] * alpha;
        a[i + 3] = rgba[i + 3];
    }
    for (int pass = 0; pass < 3; ++pass) {
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const std::size_t off = static_cast<std::size_t>(y) * w * 4;
                boxLine(a.data() + off, b.data() + off, w, 4, r);
            }
        });
        parallelRows(0, w, [&](int x0, int x1) {
            for (int x = x0; x < x1; ++x)
                boxLine(b.data() + static_cast<std::size_t>(x) * 4, a.data() + static_cast<std::size_t>(x) * 4,
                        h, w * 4, r);
        }, 8);
    }
    for (std::size_t i = 0; i < n; i += 4) {
        const float alpha = a[i + 3];
        const float inv = alpha > 0.5f ? 1.0f / (alpha / 255.0f) : 0.0f;
        rgba[i] = static_cast<std::uint8_t>(std::clamp(a[i] * inv, 0.0f, 255.0f) + 0.5f);
        rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(a[i + 1] * inv, 0.0f, 255.0f) + 0.5f);
        rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(a[i + 2] * inv, 0.0f, 255.0f) + 0.5f);
        rgba[i + 3] = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f) + 0.5f);
    }
}

}  // namespace loggx
