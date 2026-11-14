#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

}  // namespace

void Compositor::render(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba) {
    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    rgba.resize(bytes);
    for (std::size_t i = 0; i < bytes; i += 4) {
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 0;
        rgba[i + 3] = 255;
    }
    if (!m_project) return;

    std::vector<float> maskAlpha;
    // Unterste Spur zuerst: die erste Spur der Liste liegt oben.
    for (auto track = m_project->tracks.rbegin(); track != m_project->tracks.rend(); ++track) {
        if (track->kind != TrackKind::Video || track->muted) continue;
        for (const Clip& clip : track->clips) {
            if (t < clip.start || t >= clip.end()) continue;
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
            const double scale = std::min(static_cast<double>(w) / asset->width,
                                          static_cast<double>(h) / asset->height);
            const int lw = std::clamp(static_cast<int>(std::lround(asset->width * scale)), 1, w);
            const int lh = std::clamp(static_cast<int>(std::lround(asset->height * scale)), 1, h);
            const int ox = (w - lw) / 2, oy = (h - lh) / 2;
            if (!source->reader.frameAt(clip.sourceTime(t), lw, lh, m_layer)) continue;

            const double opacity = std::clamp(clip.opacity, 0.0, 1.0);
            const bool masked = clip.mask.shape != MaskShape::None;
            const bool opaque = !masked && opacity >= 0.999 && !source->reader.hasAlpha();

            for (int y = 0; y < lh; ++y) {
                const std::uint8_t* src = m_layer.data() + static_cast<std::size_t>(y) * lw * 4;
                std::uint8_t* dst =
                    rgba.data() + (static_cast<std::size_t>(y + oy) * w + ox) * 4;
                if (opaque) {
                    std::memcpy(dst, src, static_cast<std::size_t>(lw) * 4);
                    continue;
                }
                if (masked) maskRow(clip.mask, y + oy, w, h, maskAlpha);
                for (int x = 0; x < lw; ++x, src += 4, dst += 4) {
                    double a = opacity * (src[3] / 255.0);
                    if (masked) a *= maskAlpha[static_cast<std::size_t>(x + ox)];
                    if (a <= 0.0) continue;
                    const int ai = static_cast<int>(a * 256.0 + 0.5);
                    dst[0] = static_cast<std::uint8_t>((src[0] * ai + dst[0] * (256 - ai)) >> 8);
                    dst[1] = static_cast<std::uint8_t>((src[1] * ai + dst[1] * (256 - ai)) >> 8);
                    dst[2] = static_cast<std::uint8_t>((src[2] * ai + dst[2] * (256 - ai)) >> 8);
                }
            }
            break;   // pro Spur ist höchstens ein Clip aktiv
        }
    }
}

}  // namespace loggx
