#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "ImageOps.h"
#include "Parallel.h"

namespace loggx {

struct Compositor::Source {
    VideoReader reader;
    std::string path;
    bool failed = false;
};

Compositor::Compositor() = default;
Compositor::~Compositor() = default;

void Compositor::setProject(std::shared_ptr<const Project> project) {
    m_project = std::move(project);
    // Decoder von gelöschten Clips oder ausgetauschten Dateien schließen.
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        const Clip* clip = m_project ? m_project->findClip(it->first) : nullptr;
        const MediaAsset* asset = clip ? m_project->findAsset(clip->assetId) : nullptr;
        if (!asset || asset->path != it->second->path)
            it = m_sources.erase(it);
        else
            ++it;
    }
    for (auto it = m_titleCache.begin(); it != m_titleCache.end();) {
        if (!m_project || !m_project->findClip(it->first))
            it = m_titleCache.erase(it);
        else
            ++it;
    }
}

std::shared_ptr<Lut3D> Compositor::lut(const std::string& path) {
    const auto it = m_luts.find(path);
    if (it != m_luts.end()) return it->second;
    auto loaded = Lut3D::load(path);   // auch Fehlschläge merken (nullptr)
    m_luts[path] = loaded;
    return loaded;
}

namespace {

// Deckkraft der Maske (0..1) für alle Pixel einer Zeile.
void maskRow(const Mask& m, int y, int w, int h, std::vector<float>& out) {
    out.resize(static_cast<std::size_t>(w));
    const double cx = m.cx * w, cy = m.cy * h;
    const double halfW = std::max(0.5, m.w * w * 0.5), halfH = std::max(0.5, m.h * h * 0.5);
    // Mindestens 1 Pixel Übergang, damit die Kante geglättet ist.
    const double feather = std::max(1.0, m.feather * std::min(w, h));
    const double py = y + 0.5 - cy;
    for (int x = 0; x < w; ++x) {
        const double px = x + 0.5 - cx;
        double dist;   // Abstand zur Kante in Pixeln, innen negativ
        if (m.shape == MaskShape::Rectangle) {
            dist = std::max(std::abs(px) - halfW, std::abs(py) - halfH);
        } else {
            const double r = std::sqrt((px / halfW) * (px / halfW) + (py / halfH) * (py / halfH));
            dist = (r - 1.0) * std::min(halfW, halfH);
        }
        double a = std::clamp(0.5 - dist / feather, 0.0, 1.0);
        a = a * a * (3.0 - 2.0 * a);   // weicher Verlauf
        out[static_cast<std::size_t>(x)] = static_cast<float>(m.invert ? 1.0 - a : a);
    }
}

std::string titleKey(const Title& t, int w, int h) {
    std::ostringstream ss;
    ss << w << 'x' << h << '|' << t.text << '|' << t.font << '|' << t.size << '|' << t.color << '|'
       << t.bold << '|' << t.align << '|' << t.posY << '|' << t.box << '|' << t.shadow;
    return ss.str();
}

// "Über"-Komposition auf ein vormultipliziertes Ziel: Quelle mit geraden
// Farben (0..255) und Deckkraft a (0..1). Bei deckendem Ziel identisch mit
// normalem Überblenden.
inline void blendPixel(std::uint8_t* dst, float r, float g, float b, float a) {
    dst[0] = static_cast<std::uint8_t>(dst[0] + (r - dst[0]) * a + 0.5f);
    dst[1] = static_cast<std::uint8_t>(dst[1] + (g - dst[1]) * a + 0.5f);
    dst[2] = static_cast<std::uint8_t>(dst[2] + (b - dst[2]) * a + 0.5f);
    dst[3] = static_cast<std::uint8_t>(dst[3] + (255.0f - dst[3]) * a + 0.5f);
}

}  // namespace

void Compositor::render(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba) {
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    rgba.resize(bytes);
    for (std::size_t i = 0; i < bytes; i += 4) {
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 0;
        rgba[i + 3] = 255;
    }
    if (!m_project) return;
    const Project& project = *m_project;

    // Unterste Spur zuerst: die erste Spur der Liste liegt oben.
    for (auto track = project.tracks.rbegin(); track != project.tracks.rend(); ++track) {
        if (track->kind != TrackKind::Video || track->muted) continue;
        const Clip* active = nullptr;
        for (const Clip& clip : track->clips)
            if (t >= clip.start && t < clip.end()) {
                active = &clip;
                break;
            }

        // Liegt t in einem Übergang (mittig um einen Schnitt)?
        const Clip* a = nullptr;
        const Clip* b = nullptr;
        if (active) {
            const Clip* prev = project.previousAdjacent(*track, *active);
            const Clip* next = project.nextAdjacent(*track, *active);
            if (prev && active->transition != TransitionType::None) {
                const TimeUs d = std::min({active->transitionDuration, prev->duration, active->duration});
                if (t < active->start + d / 2) {
                    a = prev;
                    b = active;
                }
            }
            if (!a && next && next->transition != TransitionType::None) {
                const TimeUs d = std::min({next->transitionDuration, active->duration, next->duration});
                if (t >= next->start - d / 2) {
                    a = active;
                    b = next;
                }
            }
        }
        if (!a) {
            if (active) drawClip(*active, t, w, h, rgba, 1.0);
            continue;
        }

        const TimeUs d = std::min({b->transitionDuration, a->duration, b->duration});
        const double p = std::clamp(static_cast<double>(t - (b->start - d / 2)) / static_cast<double>(std::max<TimeUs>(1, d)), 0.0, 1.0);
        m_bufA.assign(bytes, 0);
        m_bufB.assign(bytes, 0);
        drawClip(*a, t, w, h, m_bufA, 1.0);
        drawClip(*b, t, w, h, m_bufB, 1.0);
        mixTransition(b->transition, p, w, h);
        // Ergebnis (vormultipliziert) über das bisherige Bild legen.
        const std::uint8_t* src = m_bufA.data();
        parallelRows(0, h, [&](int y0, int y1) {
            for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4) {
                const int inv = 255 - src[i + 3];
                for (int c = 0; c < 4; ++c)
                    rgba[i + c] = static_cast<std::uint8_t>(std::min(255, src[i + c] + (rgba[i + c] * inv + 127) / 255));
            }
        });
    }
}

void Compositor::mixTransition(TransitionType type, double p, int w, int h) {
    std::uint8_t* A = m_bufA.data();
    const std::uint8_t* B = m_bufB.data();
    const float fp = static_cast<float>(p);
    switch (type) {
        case TransitionType::DipBlack:
        case TransitionType::DipWhite: {
            // Erst A in die Farbe, dann aus der Farbe zu B.
            const float c = type == TransitionType::DipWhite ? 255.0f : 0.0f;
            const bool first = p < 0.5;
            const float k = first ? fp * 2.0f : fp * 2.0f - 1.0f;
            parallelRows(0, h, [&](int y0, int y1) {
                for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; i += 4) {
                    const std::uint8_t* s = first ? A + i : B + i;
                    const float ws = first ? 1.0f - k : k;   // Anteil des Clips
                    for (int ch = 0; ch < 3; ++ch) A[i + ch] = static_cast<std::uint8_t>(s[ch] * ws + c * (1.0f - ws) + 0.5f);
                    A[i + 3] = static_cast<std::uint8_t>(s[3] * ws + 255.0f * (1.0f - ws) + 0.5f);
                }
            });
            break;
        }
        case TransitionType::Wipe: {
            // Von links nach rechts aufdecken, weiche Kante 3 % der Breite.
            const float edge = std::max(1.0f, w * 0.03f);
            const float pos = fp * (w + 2 * edge) - edge;
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x) {
                        float k = std::clamp((pos - x) / edge + 0.5f, 0.0f, 1.0f);
                        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                        for (int ch = 0; ch < 4; ++ch)
                            A[i + ch] = static_cast<std::uint8_t>(A[i + ch] * (1.0f - k) + B[i + ch] * k + 0.5f);
                    }
            });
            break;
        }
        case TransitionType::Slide: {
            // A schiebt nach links hinaus, B kommt von rechts.
            const int shift = static_cast<int>(std::lround(p * w));
            parallelRows(0, h, [&](int y0, int y1) {
                std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 4);
                for (int y = y0; y < y1; ++y) {
                    std::uint8_t* rowA = A + static_cast<std::size_t>(y) * w * 4;
                    const std::uint8_t* rowB = B + static_cast<std::size_t>(y) * w * 4;
                    for (int x = 0; x < w; ++x) {
                        const int sa = x + shift;
                        const std::uint8_t* s = sa < w ? rowA + sa * 4 : rowB + (sa - w) * 4;
                        std::memcpy(row.data() + x * 4, s, 4);
                    }
                    std::memcpy(rowA, row.data(), row.size());
                }
            });
            break;
        }
        case TransitionType::WipeUp: {
            // Von unten nach oben aufdecken.
            const float edge = std::max(1.0f, h * 0.04f);
            const float pos = fp * (h + 2 * edge) - edge;   // von unten gemessen
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const float k = std::clamp((pos - (h - 1 - y)) / edge + 0.5f, 0.0f, 1.0f);
                    std::uint8_t* rowA = A + static_cast<std::size_t>(y) * w * 4;
                    const std::uint8_t* rowB = B + static_cast<std::size_t>(y) * w * 4;
                    for (int i = 0; i < w * 4; ++i)
                        rowA[i] = static_cast<std::uint8_t>(rowA[i] * (1.0f - k) + rowB[i] * k + 0.5f);
                }
            });
            break;
        }
        case TransitionType::SlideUp: {
            // A schiebt nach oben hinaus, B kommt von unten.
            const int shift = static_cast<int>(std::lround(p * h));
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
            std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const int sy = y + shift;
                    const std::uint8_t* src = sy < h ? A + static_cast<std::size_t>(sy) * rowBytes : B + static_cast<std::size_t>(sy - h) * rowBytes;
                    std::memcpy(out.data() + static_cast<std::size_t>(y) * rowBytes, src, rowBytes);
                }
            });
            std::memcpy(A, out.data(), out.size());
            break;
        }
        case TransitionType::Circle: {
            // B erscheint in einem wachsenden Kreis aus der Mitte.
            const float maxR = 0.5f * std::sqrt(static_cast<float>(w) * w + static_cast<float>(h) * h);
            const float edge = std::max(1.5f, h * 0.02f);
            const float radius = fp * (maxR + edge);
            const float cx = w / 2.0f, cy = h / 2.0f;
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const float dy = y + 0.5f - cy;
                    for (int x = 0; x < w; ++x) {
                        const float dx = x + 0.5f - cx;
                        const float k = std::clamp((radius - std::sqrt(dx * dx + dy * dy)) / edge + 0.5f, 0.0f, 1.0f);
                        if (k <= 0.0f) continue;
                        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                        for (int ch = 0; ch < 4; ++ch)
                            A[i + ch] = static_cast<std::uint8_t>(A[i + ch] * (1.0f - k) + B[i + ch] * k + 0.5f);
                    }
                }
            });
            break;
        }
        case TransitionType::Zoom: {
            // A zoomt hinein und blendet aus, B kommt aus der Vergrößerung zurück.
            const std::vector<std::uint8_t> srcA(A, A + static_cast<std::size_t>(w) * h * 4);
            const float za = 1.0f + 0.6f * fp, zb = 1.6f - 0.6f * fp;
            const float mixB = std::clamp((fp - 0.2f) / 0.6f, 0.0f, 1.0f);
            auto sample = [&](const std::uint8_t* img, float zoom, int x, int y, int ch) {
                const float sx = (x + 0.5f - w / 2.0f) / zoom + w / 2.0f - 0.5f;
                const float sy = (y + 0.5f - h / 2.0f) / zoom + h / 2.0f - 0.5f;
                const int ix = std::clamp(static_cast<int>(sx), 0, w - 2), iy = std::clamp(static_cast<int>(sy), 0, h - 2);
                const float tx = std::clamp(sx - ix, 0.0f, 1.0f), ty = std::clamp(sy - iy, 0.0f, 1.0f);
                const std::uint8_t* q = img + (static_cast<std::size_t>(iy) * w + ix) * 4 + ch;
                const std::size_t row = static_cast<std::size_t>(w) * 4;
                return (q[0] * (1 - tx) + q[4] * tx) * (1 - ty) + (q[row] * (1 - tx) + q[row + 4] * tx) * ty;
            };
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x) {
                        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                        for (int ch = 0; ch < 4; ++ch)
                            A[i + ch] = static_cast<std::uint8_t>(sample(srcA.data(), za, x, y, ch) * (1.0f - mixB) +
                                                                   sample(B, zb, x, y, ch) * mixB + 0.5f);
                    }
            });
            break;
        }
        case TransitionType::BlurDissolve:
        case TransitionType::Pixelate: {
            // In der Mitte am stärksten weichgezeichnet bzw. verpixelt.
            const double amount = std::sin(p * 3.14159265358979323846);
            if (type == TransitionType::BlurDissolve) {
                applyBlur(m_bufA, w, h, amount * h * 0.03);
                applyBlur(m_bufB, w, h, amount * h * 0.03);
            } else {
                const int block = 1 + static_cast<int>(std::lround(amount * h * 0.06));
                applyPixelate(m_bufA, w, h, block);
                applyPixelate(m_bufB, w, h, block);
            }
            const float k = type == TransitionType::Pixelate ? std::clamp((fp - 0.4f) / 0.2f, 0.0f, 1.0f) : fp;
            const std::uint8_t* Bn = m_bufB.data();
            parallelRows(0, h, [&](int y0, int y1) {
                for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; ++i)
                    A[i] = static_cast<std::uint8_t>(A[i] * (1.0f - k) + Bn[i] * k + 0.5f);
            });
            break;
        }
        default: {   // Überblenden
            parallelRows(0, h, [&](int y0, int y1) {
                for (std::size_t i = static_cast<std::size_t>(y0) * w * 4; i < static_cast<std::size_t>(y1) * w * 4; ++i)
                    A[i] = static_cast<std::uint8_t>(A[i] * (1.0f - fp) + B[i] * fp + 0.5f);
            });
            break;
        }
    }
}

void Compositor::drawClip(const Clip& clip, TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba,
                          double extraOpacity) {

    // Blenden nur innerhalb des Clips auswerten (bei Übergängen wird über
    // das Ende hinaus gezeichnet).
    const double opacity = std::clamp(clip.valueAt("opacity", t), 0.0, 1.0) *
                           clip.fadeAt(std::clamp(t, clip.start, clip.end() - 1)) * extraOpacity;
    if (opacity <= 0.001) return;
    const double scale = std::clamp(clip.valueAt("scale", t), 0.01, 20.0);

    // ---- Ebene erzeugen (in Ebenen-Pixeln)
    double baseW, baseH;   // Größe im Ausgabebild bei Skalierung 1
    int lw, lh;
    if (clip.kind == ClipKind::Media) {
        const MediaAsset* asset = m_project->findAsset(clip.assetId);
        if (!asset || !asset->hasVideo || asset->width <= 0 || asset->height <= 0) return;
        auto& source = m_sources[clip.id];
        if (!source) {
            source = std::make_unique<Source>();
            source->path = asset->path;
            source->failed = !source->reader.open(asset->path);
        }
        if (source->failed) return;
        // Seitenverhältnis erhalten und ins Bild einpassen.
        const double fit = std::min(static_cast<double>(w) / asset->width,
                                    static_cast<double>(h) / asset->height);
        baseW = asset->width * fit;
        baseH = asset->height * fit;
        // Beim Vergrößern höchstens in Quellauflösung dekodieren.
        const double k = std::min(scale, std::max(1.0, asset->width / baseW));
        lw = std::max(1, static_cast<int>(std::lround(baseW * k)));
        lh = std::max(1, static_cast<int>(std::lround(baseH * k)));
        if (!source->reader.frameAt(clip.sourceTime(t), lw, lh, m_layer)) return;
    } else if (clip.kind == ClipKind::Title) {
        if (!m_titles) return;
        baseW = w;
        baseH = h;
        lw = w;
        lh = h;
        auto& cache = m_titleCache[clip.id];
        const std::string key = titleKey(clip.title, w, h);
        if (cache.key != key) {
            cache.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
            m_titles(clip.title, w, h, cache.rgba);
            if (cache.rgba.size() != static_cast<std::size_t>(w) * h * 4)
                cache.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
            cache.key = key;
        }
        m_layer = cache.rgba;
    } else {
        return;
    }

    // ---- Effekte auf der Ebene
    if (!clip.color.isNeutral()) {
        std::shared_ptr<Lut3D> l = clip.color.lutPath.empty() ? nullptr : lut(clip.color.lutPath);
        applyColorGrade(m_layer, lw, lh, clip.color, l.get());
    }
    const Effects& fx = clip.fx;
    if (fx.chromaKey) applyChromaKey(m_layer, lw, lh, fx);
    if (fx.mirrorH || fx.mirrorV) applyMirror(m_layer, lw, lh, fx.mirrorH, fx.mirrorV);
    if (fx.pixelate > 0.0) applyPixelate(m_layer, lw, lh, std::max(2, static_cast<int>(std::lround(fx.pixelate * 0.08 * lh))));
    const double blur = clip.valueAt("blur", t);
    if (blur > 0.0) applyBlur(m_layer, lw, lh, blur * h * lh / (baseH * scale));
    if (fx.sharpen > 0.0) applySharpen(m_layer, lw, lh, fx.sharpen);
    if (fx.glow > 0.0) applyGlow(m_layer, lw, lh, fx.glow);
    if (fx.rgbSplit > 0.0) applyRgbSplit(m_layer, lw, lh, static_cast<int>(std::lround(fx.rgbSplit * 0.015 * lw)));
    if (fx.sepia > 0.0) applySepia(m_layer, lw, lh, fx.sepia);
    if (fx.invert) applyInvert(m_layer, lw, lh);
    if (fx.vignette > 0.0) applyVignette(m_layer, lw, lh, fx.vignette);
    // Korn ändert sich jedes Bild (Zeit als Startwert)
    if (fx.grain > 0.0) applyGrain(m_layer, lw, lh, fx.grain, static_cast<std::uint32_t>(t / 20'000) * 2654435761u + 12345u);

    // ---- Ins Bild setzen
    const double cx = w / 2.0 + clip.valueAt("x", t) * w;
    const double cy = h / 2.0 + clip.valueAt("y", t) * h;
    const double angle = clip.valueAt("rotation", t) * 3.14159265358979323846 / 180.0;
    const double outW = baseW * scale, outH = baseH * scale;
    const bool masked = clip.mask.shape != MaskShape::None;
    const float op = static_cast<float>(opacity);
    const std::uint8_t* layer = m_layer.data();

    const bool axisAligned = std::abs(angle) < 1e-9 && std::abs(outW - lw) < 0.51 &&
                             std::abs(outH - lh) < 0.51;
    if (axisAligned) {
        // Schneller Weg: 1:1 kopieren mit Versatz.
        const int ox = static_cast<int>(std::lround(cx - lw / 2.0));
        const int oy = static_cast<int>(std::lround(cy - lh / 2.0));
        const int x0 = std::max(0, ox), x1 = std::min(w, ox + lw);
        const int y0 = std::max(0, oy), y1 = std::min(h, oy + lh);
        if (x0 >= x1 || y0 >= y1) return;
        parallelRows(y0, y1, [&](int ya, int yb) {
            std::vector<float> maskBuf;
            for (int y = ya; y < yb; ++y) {
                if (masked) maskRow(clip.mask, y, w, h, maskBuf);
                const std::uint8_t* src = layer + (static_cast<std::size_t>(y - oy) * lw + (x0 - ox)) * 4;
                std::uint8_t* dst = rgba.data() + (static_cast<std::size_t>(y) * w + x0) * 4;
                for (int x = x0; x < x1; ++x, src += 4, dst += 4) {
                    float a = op * (src[3] / 255.0f);
                    if (masked) a *= maskBuf[static_cast<std::size_t>(x)];
                    if (a <= 0.0f) continue;
                    if (a >= 0.999f) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 255;
                    } else {
                        blendPixel(dst, src[0], src[1], src[2], a);
                    }
                }
            }
        });
        return;
    }

    // Allgemeiner Weg: Rückwärts-Abbildung mit bilinearer Abtastung.
    const double cs = std::cos(angle), sn = std::sin(angle);
    const double hw = outW / 2.0, hh = outH / 2.0;
    const double ex = std::abs(hw * cs) + std::abs(hh * sn);
    const double ey = std::abs(hw * sn) + std::abs(hh * cs);
    const int bx0 = std::max(0, static_cast<int>(std::floor(cx - ex)) - 1);
    const int bx1 = std::min(w, static_cast<int>(std::ceil(cx + ex)) + 1);
    const int by0 = std::max(0, static_cast<int>(std::floor(cy - ey)) - 1);
    const int by1 = std::min(h, static_cast<int>(std::ceil(cy + ey)) + 1);
    if (bx0 >= bx1 || by0 >= by1) return;
    const double sx = lw / outW, sy = lh / outH;

    parallelRows(by0, by1, [&](int ya, int yb) {
        std::vector<float> maskBuf;
        for (int y = ya; y < yb; ++y) {
            if (masked) maskRow(clip.mask, y, w, h, maskBuf);
            std::uint8_t* dst = rgba.data() + (static_cast<std::size_t>(y) * w + bx0) * 4;
            const double dy = y + 0.5 - cy;
            for (int x = bx0; x < bx1; ++x, dst += 4) {
                const double dx = x + 0.5 - cx;
                const double u = dx * cs + dy * sn + hw;     // Ebene, Ausgabe-Pixel
                const double v = -dx * sn + dy * cs + hh;
                const double fx = u * sx - 0.5, fy = v * sy - 0.5;
                if (fx < -1.0 || fy < -1.0 || fx > lw || fy > lh) continue;
                const int ix = static_cast<int>(std::floor(fx));
                const int iy = static_cast<int>(std::floor(fy));
                const float ax = static_cast<float>(fx - ix), ay = static_cast<float>(fy - iy);
                float acc[4] = {0, 0, 0, 0};
                for (int j = 0; j < 2; ++j) {
                    const int yy = iy + j;
                    if (yy < 0 || yy >= lh) continue;
                    const float wy = j ? ay : 1.0f - ay;
                    for (int i = 0; i < 2; ++i) {
                        const int xx = ix + i;
                        if (xx < 0 || xx >= lw) continue;
                        const float wgt = wy * (i ? ax : 1.0f - ax);
                        const std::uint8_t* s = layer + (static_cast<std::size_t>(yy) * lw + xx) * 4;
                        const float sa = s[3] * wgt;
                        acc[0] += s[0] * sa;
                        acc[1] += s[1] * sa;
                        acc[2] += s[2] * sa;
                        acc[3] += sa;
                    }
                }
                if (acc[3] <= 0.0f) continue;
                float a = op * acc[3] / 255.0f;
                if (masked) a *= maskBuf[static_cast<std::size_t>(x)];
                if (a <= 0.0f) continue;
                blendPixel(dst, acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], std::min(a, 1.0f));
            }
        }
    });
}

}  // namespace loggx
