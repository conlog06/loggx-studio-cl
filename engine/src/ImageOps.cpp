#include "ImageOps.h"

#include <algorithm>
#include <array>
#include <cstring>
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

    // Schneller Weg ohne LUT: alles in 8-Bit-Tabellen und Ganzzahlen.
    if (mix <= 0.0f) {
        std::array<std::array<std::uint8_t, 256>, 3> bt;
        for (int c = 0; c < 3; ++c)
            for (int i = 0; i < 256; ++i)
                bt[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                    table[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] * 255.0f + 0.5f);
        const int satQ = static_cast<int>(std::lround(sat * 256.0f));
        const bool doSat = satQ != 256;
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x, p += 4) {
                    int r = bt[0][p[0]], gg = bt[1][p[1]], b = bt[2][p[2]];
                    if (doSat) {
                        const int luma = (54 * r + 183 * gg + 19 * b) >> 8;
                        r = luma + (((r - luma) * satQ) >> 8);
                        gg = luma + (((gg - luma) * satQ) >> 8);
                        b = luma + (((b - luma) * satQ) >> 8);
                        r = std::clamp(r, 0, 255);
                        gg = std::clamp(gg, 0, 255);
                        b = std::clamp(b, 0, 255);
                    }
                    p[0] = static_cast<std::uint8_t>(r);
                    p[1] = static_cast<std::uint8_t>(gg);
                    p[2] = static_cast<std::uint8_t>(b);
                }
            }
        });
        return;
    }

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
    // Abdunklung hängt nur von d² ab: Tabelle über d² (0..1) statt Wurzel je Pixel.
    constexpr int kSteps = 1024;
    std::array<std::uint16_t, kSteps + 1> fac;
    for (int i = 0; i <= kSteps; ++i) {
        const float d = std::sqrt(static_cast<float>(i) / kSteps);
        float v = std::clamp((d - 0.35f) / 0.65f, 0.0f, 1.0f);
        v = v * v * (3.0f - 2.0f * v);
        fac[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>((1.0f - k * v) * 256.0f + 0.5f);
    }
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float ny = (y + 0.5f) / h * 2.0f - 1.0f;
            const float ny2 = ny * ny * 0.5f;
            std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                const float nx = (x + 0.5f) / w * 2.0f - 1.0f;
                const int idx = std::min(kSteps, static_cast<int>((nx * nx * 0.5f + ny2) * kSteps));
                const unsigned f = fac[static_cast<std::size_t>(idx)];
                p[0] = static_cast<std::uint8_t>((p[0] * f) >> 8);
                p[1] = static_cast<std::uint8_t>((p[1] * f) >> 8);
                p[2] = static_cast<std::uint8_t>((p[2] * f) >> 8);
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
    // Mitte ohne Randprüfung, nur die Ränder mit Klemmung.
    const int safeA = r, safeB = n - r - 1;
    for (int i = 0; i < n; ++i) {
        float* o = out + static_cast<std::ptrdiff_t>(i) * stride;
        for (int c = 0; c < 4; ++c) o[c] = sum[c] * norm;
        const float* add;
        const float* sub;
        if (i >= safeA && i < safeB) {
            add = in + static_cast<std::ptrdiff_t>(i + r + 1) * stride;
            sub = in + static_cast<std::ptrdiff_t>(i - r) * stride;
        } else {
            add = px(i + r + 1);
            sub = px(i - r);
        }
        for (int c = 0; c < 4; ++c) sum[c] += add[c] - sub[c];
    }
}

}  // namespace

namespace {

// Senkrechter Box-Filter über einen Spaltenstreifen [x0, x1): läuft zeilenweise
// mit laufenden Summen – speicherfreundlich statt Spalte für Spalte.
void boxColumns(const float* in, float* out, int w, int h, int x0, int x1, int r) {
    const int n = (x1 - x0) * 4;
    const float norm = 1.0f / static_cast<float>(2 * r + 1);
    std::vector<float> sum(static_cast<std::size_t>(n), 0.0f);
    auto row = [&](int y) { return in + (static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w + x0) * 4; };
    for (int y = -r; y <= r; ++y) {
        const float* src = row(y);
        for (int i = 0; i < n; ++i) sum[static_cast<std::size_t>(i)] += src[i];
    }
    for (int y = 0; y < h; ++y) {
        float* o = out + (static_cast<std::size_t>(y) * w + x0) * 4;
        const float* add = row(y + r + 1);
        const float* sub = row(y - r);
        for (int i = 0; i < n; ++i) {
            o[i] = sum[static_cast<std::size_t>(i)] * norm;
            sum[static_cast<std::size_t>(i)] += add[i] - sub[i];
        }
    }
}

void blurFloat(std::vector<float>& a, std::vector<float>& b, int w, int h, int r) {
    for (int pass = 0; pass < 3; ++pass) {
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const std::size_t off = static_cast<std::size_t>(y) * w * 4;
                boxLine(a.data() + off, b.data() + off, w, 4, r);
            }
        });
        parallelRows(0, w, [&](int x0, int x1) { boxColumns(b.data(), a.data(), w, h, x0, x1, r); }, 64);
    }
}

}  // namespace

void applyBlur(std::vector<std::uint8_t>& rgba, int w, int h, double radius) {
    if (radius < 0.5 || w < 2 || h < 2) return;
    // Große Radien auf verkleinertem Bild rechnen (2× oder 4×): sieht gleich
    // aus, kostet aber nur einen Bruchteil.
    const int factor = radius >= 20 ? 4 : radius >= 6 ? 2 : 1;
    const int sw = std::max(2, w / factor), sh = std::max(2, h / factor);
    const int r = std::max(1, static_cast<int>(std::lround(radius * 0.58 / factor)));
    const std::size_t n = static_cast<std::size_t>(sw) * sh * 4;
    std::vector<float> a(n), b(n);
    // Vormultipliziert einlesen (bei Verkleinerung als Mittelwert der Blöcke).
    parallelRows(0, sh, [&](int y0, int y1) {
        const float inv = 1.0f / static_cast<float>(factor * factor);
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < sw; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int j = 0; j < factor; ++j) {
                    const std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(std::min(h - 1, y * factor + j)) * w + x * factor) * 4;
                    for (int i = 0; i < factor; ++i, p += 4) {
                        const float alpha = p[3] / 255.0f;
                        acc[0] += p[0] * alpha;
                        acc[1] += p[1] * alpha;
                        acc[2] += p[2] * alpha;
                        acc[3] += p[3];
                    }
                }
                float* o = a.data() + (static_cast<std::size_t>(y) * sw + x) * 4;
                for (int c = 0; c < 4; ++c) o[c] = acc[c] * inv;
            }
    });
    blurFloat(a, b, sw, sh, r);
    // Zurückschreiben (bei Verkleinerung bilinear vergrößert).
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float fy = std::clamp((y + 0.5f) / factor - 0.5f, 0.0f, static_cast<float>(sh - 1));
            const int iy = std::min(sh - 2, static_cast<int>(fy));
            const float ty = factor == 1 ? 0.0f : fy - iy;
            const int yy = factor == 1 ? y : iy;
            std::uint8_t* d = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, d += 4) {
                float px[4];
                if (factor == 1) {
                    const float* s = a.data() + (static_cast<std::size_t>(yy) * sw + x) * 4;
                    for (int c = 0; c < 4; ++c) px[c] = s[c];
                } else {
                    const float fx = std::clamp((x + 0.5f) / factor - 0.5f, 0.0f, static_cast<float>(sw - 1));
                    const int ix = std::min(sw - 2, static_cast<int>(fx));
                    const float tx = fx - ix;
                    const float* s00 = a.data() + (static_cast<std::size_t>(iy) * sw + ix) * 4;
                    const float* s01 = s00 + 4;
                    const float* s10 = s00 + static_cast<std::size_t>(sw) * 4;
                    const float* s11 = s10 + 4;
                    for (int c = 0; c < 4; ++c)
                        px[c] = (s00[c] * (1 - tx) + s01[c] * tx) * (1 - ty) + (s10[c] * (1 - tx) + s11[c] * tx) * ty;
                }
                const float alpha = px[3];
                const float inv = alpha > 0.5f ? 255.0f / alpha : 0.0f;
                d[0] = static_cast<std::uint8_t>(std::clamp(px[0] * inv, 0.0f, 255.0f) + 0.5f);
                d[1] = static_cast<std::uint8_t>(std::clamp(px[1] * inv, 0.0f, 255.0f) + 0.5f);
                d[2] = static_cast<std::uint8_t>(std::clamp(px[2] * inv, 0.0f, 255.0f) + 0.5f);
                d[3] = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f) + 0.5f);
            }
        }
    });
}

// ------------------------------------------------------- weitere Effekte

void applyMirror(std::vector<std::uint8_t>& rgba, int w, int h, bool horizontal, bool vertical) {
    auto* px = reinterpret_cast<std::uint32_t*>(rgba.data());
    if (horizontal)
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) std::reverse(px + static_cast<std::size_t>(y) * w, px + static_cast<std::size_t>(y + 1) * w);
        });
    if (vertical)
        for (int y = 0; y < h / 2; ++y)
            std::swap_ranges(px + static_cast<std::size_t>(y) * w, px + static_cast<std::size_t>(y + 1) * w,
                             px + static_cast<std::size_t>(h - 1 - y) * w);
}

void applyPixelate(std::vector<std::uint8_t>& rgba, int w, int h, int block) {
    if (block < 2) return;
    const int rows = (h + block - 1) / block;
    parallelRows(0, rows, [&](int r0, int r1) {
        for (int r = r0; r < r1; ++r) {
            const int y0 = r * block, y1 = std::min(h, y0 + block);
            for (int x0 = 0; x0 < w; x0 += block) {
                const int x1 = std::min(w, x0 + block);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int y = y0; y < y1; ++y) {
                    const std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x0) * 4;
                    for (int x = x0; x < x1; ++x, p += 4)
                        for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
                const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
                std::uint8_t avg[4];
                for (int c = 0; c < 4; ++c) avg[c] = static_cast<std::uint8_t>((sum[c] + n / 2) / n);
                for (int y = y0; y < y1; ++y) {
                    std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x0) * 4;
                    for (int x = x0; x < x1; ++x, p += 4) std::memcpy(p, avg, 4);
                }
            }
        }
    }, 1);
}

void applySharpen(std::vector<std::uint8_t>& rgba, int w, int h, double amount) {
    // Unscharfe Maske: Original + k·(Original − Weichgezeichnet)
    std::vector<std::uint8_t> soft = rgba;
    applyBlur(soft, w, h, std::max(1.2, h / 600.0));
    const int k = static_cast<int>(std::lround(std::clamp(amount, 0.0, 1.0) * 2.5 * 256));
    parallelRows(0, h, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4)
            for (int c = 0; c < 3; ++c) {
                const int v = rgba[i + c] + (((rgba[i + c] - soft[i + c]) * k) >> 8);
                rgba[i + c] = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
            }
    });
}

void applyGlow(std::vector<std::uint8_t>& rgba, int w, int h, double amount) {
    // Helle Bereiche weichzeichnen und mit „Negativ multiplizieren“ (Screen) darüberlegen.
    std::vector<std::uint8_t> bright(rgba.size());
    parallelRows(0, h, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4) {
            const int luma = (54 * rgba[i] + 183 * rgba[i + 1] + 19 * rgba[i + 2]) >> 8;
            const int keep = std::clamp((luma - 150) * 3, 0, 255);
            for (int c = 0; c < 3; ++c) bright[i + c] = static_cast<std::uint8_t>((rgba[i + c] * keep) >> 8);
            bright[i + 3] = 255;
        }
    });
    applyBlur(bright, w, h, std::max(2.0, h * 0.025));
    const int k = static_cast<int>(std::lround(std::clamp(amount, 0.0, 1.0) * 1.5 * 256));
    parallelRows(0, h, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4)
            for (int c = 0; c < 3; ++c) {
                const int g = std::min(255, (bright[i + c] * k) >> 8);
                rgba[i + c] = static_cast<std::uint8_t>(255 - ((255 - rgba[i + c]) * (255 - g) + 127) / 255);
            }
    });
}

void applyRgbSplit(std::vector<std::uint8_t>& rgba, int w, int h, int shift) {
    if (shift < 1) return;
    const std::vector<std::uint8_t> src = rgba;
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* row = src.data() + static_cast<std::size_t>(y) * w * 4;
            std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                out[x * 4] = row[std::clamp(x + shift, 0, w - 1) * 4];           // Rot nach links
                out[x * 4 + 2] = row[std::clamp(x - shift, 0, w - 1) * 4 + 2];   // Blau nach rechts
            }
        }
    });
}

void applySepia(std::vector<std::uint8_t>& rgba, int w, int h, double amount) {
    const int k = static_cast<int>(std::lround(std::clamp(amount, 0.0, 1.0) * 256));
    parallelRows(0, h, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4) {
            const int r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
            const int sr = std::min(255, (r * 101 + g * 197 + b * 48) >> 8);
            const int sg = std::min(255, (r * 89 + g * 176 + b * 43) >> 8);
            const int sb = std::min(255, (r * 70 + g * 137 + b * 34) >> 8);
            rgba[i] = static_cast<std::uint8_t>(r + (((sr - r) * k) >> 8));
            rgba[i + 1] = static_cast<std::uint8_t>(g + (((sg - g) * k) >> 8));
            rgba[i + 2] = static_cast<std::uint8_t>(b + (((sb - b) * k) >> 8));
        }
    });
}

void applyInvert(std::vector<std::uint8_t>& rgba, int w, int h) {
    parallelRows(0, h, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4)
            for (int c = 0; c < 3; ++c) rgba[i + c] = static_cast<std::uint8_t>(255 - rgba[i + c]);
    });
}

void applyGrain(std::vector<std::uint8_t>& rgba, int w, int h, double amount, std::uint32_t seed) {
    const int k = static_cast<int>(std::lround(std::clamp(amount, 0.0, 1.0) * 60));
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            std::uint32_t s = seed ^ (static_cast<std::uint32_t>(y) * 0x9E3779B9u);
            std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                s ^= s << 13;   // xorshift
                s ^= s >> 17;
                s ^= s << 5;
                const int n = static_cast<int>((s >> 16) & 0xFF) + static_cast<int>((s >> 8) & 0xFF) - 255;   // ≈ Dreieck
                const int d = (n * k) >> 8;
                for (int c = 0; c < 3; ++c) p[c] = static_cast<std::uint8_t>(std::clamp(p[c] + d, 0, 255));
            }
        }
    });
}

}  // namespace loggx
