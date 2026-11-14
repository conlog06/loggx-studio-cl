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

inline void blendPixel(std::uint8_t* dst, float r, float g, float b, float a) {
    // a ist 0..1, Farben nicht vormultipliziert 0..255
    dst[0] = static_cast<std::uint8_t>(dst[0] + (r - dst[0]) * a + 0.5f);
    dst[1] = static_cast<std::uint8_t>(dst[1] + (g - dst[1]) * a + 0.5f);
    dst[2] = static_cast<std::uint8_t>(dst[2] + (b - dst[2]) * a + 0.5f);
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

    // Unterste Spur zuerst: die erste Spur der Liste liegt oben.
    for (auto track = m_project->tracks.rbegin(); track != m_project->tracks.rend(); ++track) {
        if (track->kind != TrackKind::Video || track->muted) continue;
        const Clip* active = nullptr;
        for (const Clip& clip : track->clips)
            if (t >= clip.start && t < clip.end()) {
                active = &clip;
                break;
            }
        if (!active) continue;
        const Clip& clip = *active;

        const double opacity =
            std::clamp(clip.valueAt("opacity", t), 0.0, 1.0) * clip.fadeAt(t);
        if (opacity <= 0.001) continue;
        const double scale = std::clamp(clip.valueAt("scale", t), 0.01, 20.0);

        // ---- Ebene erzeugen (in Ebenen-Pixeln)
        double baseW, baseH;   // Größe im Ausgabebild bei Skalierung 1
        int lw, lh;
        if (clip.kind == ClipKind::Media) {
            const MediaAsset* asset = m_project->findAsset(clip.assetId);
            if (!asset || !asset->hasVideo || asset->width <= 0 || asset->height <= 0) continue;
            auto& source = m_sources[clip.id];
            if (!source) {
                source = std::make_unique<Source>();
                source->path = asset->path;
                source->failed = !source->reader.open(asset->path);
            }
            if (source->failed) continue;
            // Seitenverhältnis erhalten und ins Bild einpassen.
            const double fit = std::min(static_cast<double>(w) / asset->width,
                                        static_cast<double>(h) / asset->height);
            baseW = asset->width * fit;
            baseH = asset->height * fit;
            // Beim Vergrößern höchstens in Quellauflösung dekodieren.
            const double k = std::min(scale, std::max(1.0, asset->width / baseW));
            lw = std::max(1, static_cast<int>(std::lround(baseW * k)));
            lh = std::max(1, static_cast<int>(std::lround(baseH * k)));
            if (!source->reader.frameAt(clip.sourceTime(t), lw, lh, m_layer)) continue;
        } else if (clip.kind == ClipKind::Title) {
            if (!m_titles) continue;
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
            continue;
        }

        // ---- Effekte auf der Ebene
        if (!clip.color.isNeutral()) {
            std::shared_ptr<Lut3D> l = clip.color.lutPath.empty() ? nullptr : lut(clip.color.lutPath);
            applyColorGrade(m_layer, lw, lh, clip.color, l.get());
        }
        if (clip.fx.chromaKey) applyChromaKey(m_layer, lw, lh, clip.fx);
        const double blur = clip.valueAt("blur", t);
        if (blur > 0.0) applyBlur(m_layer, lw, lh, blur * h * lh / (baseH * scale));
        if (clip.fx.vignette > 0.0) applyVignette(m_layer, lw, lh, clip.fx.vignette);

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
            if (x0 >= x1 || y0 >= y1) continue;
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
                        } else {
                            blendPixel(dst, src[0], src[1], src[2], a);
                        }
                    }
                }
            });
            continue;
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
        if (bx0 >= bx1 || by0 >= by1) continue;
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
}

}  // namespace loggx
