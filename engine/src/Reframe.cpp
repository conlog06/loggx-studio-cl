// Auto-Reframe: bringt Clips in ein anderes Seitenverhältnis (z. B. 16:9 →
// 9:16 für Reels/TikTok) und schwenkt dabei mit dem Motiv mit.
// Motiv je Stichprobe (alle 0,25 s): KI-Freistellung, wenn ein Modell da ist,
// sonst Bewegung + Kanten mit leichtem Mittenvorzug. Der Weg wird geglättet
// und wie bei einem ruhigen Kameramann nur bewegt, wenn das Motiv aus der
// Mitte läuft (Totzone).
#include <algorithm>
#include <cmath>

#include "loggx/engine/Engine.h"

namespace loggx {

namespace {
struct Point {
    double u = 0.5, v = 0.5, weight = 0;
};

Point subjectOf(const std::vector<std::uint8_t>& rgba, const std::vector<std::uint8_t>& prev, int w, int h, bool useAi) {
    Point p;
    double sx = 0, sy = 0, sw = 0;
    if (useAi) {
        std::vector<std::uint8_t> alpha;
        if (cutoutMask(rgba.data(), w, h, alpha)) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const double a = alpha[static_cast<std::size_t>(y) * w + x] / 255.0;
                    if (a < 0.3) continue;
                    sx += a * x;
                    sy += a * y;
                    sw += a;
                }
            if (sw > w * h * 0.01) return {sx / sw / w, sy / sw / h, sw / (w * h)};
            sx = sy = sw = 0;
        }
    }
    // Ohne KI: Bewegung (Unterschied zum letzten Bild) + Kanten, Mitte leicht bevorzugt
    auto lum = [&](const std::vector<std::uint8_t>& img, int x, int y) {
        const std::uint8_t* q = &img[(static_cast<std::size_t>(y) * w + x) * 4];
        return 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
    };
    for (int y = 1; y < h - 1; y += 2)
        for (int x = 1; x < w - 1; x += 2) {
            const double l = lum(rgba, x, y);
            const double edge = std::abs(lum(rgba, x + 1, y) - lum(rgba, x - 1, y)) + std::abs(lum(rgba, x, y + 1) - lum(rgba, x, y - 1));
            const double motion = prev.empty() ? 0.0 : std::abs(l - lum(prev, x, y));
            const double dx = (x + 0.5) / w - 0.5, dy = (y + 0.5) / h - 0.5;
            const double centre = std::exp(-(dx * dx + dy * dy) * 4.0);
            const double e = (motion * 3.0 + edge * 0.5) * (0.4 + 0.6 * centre);
            if (e < 8.0) continue;
            sx += e * x;
            sy += e * y;
            sw += e;
        }
    if (sw <= 0) return p;
    return {sx / sw / w, sy / sw / h, 0.0};
}

// Glätten: Median gegen Ausreißer, dann Totzone + gleitender Mittelwert
std::vector<double> smoothPath(const std::vector<double>& raw, double deadZone) {
    const std::size_t n = raw.size();
    std::vector<double> med(n), out(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<double> win;
        for (std::size_t j = i >= 3 ? i - 3 : 0; j < std::min(n, i + 4); ++j) win.push_back(raw[j]);
        std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
        med[i] = win[win.size() / 2];
    }
    // Kameramann mit Totzone – vorwärts und rückwärts, gemittelt (keine Verzögerung)
    std::vector<double> fwd(n), bwd(n);
    auto follow = [&](double& cam, double target) {
        const double d = target - cam;
        if (std::abs(d) > deadZone) cam += (d - std::copysign(deadZone, d)) * 0.8;
    };
    double cam = n ? med[0] : 0.5;
    for (std::size_t i = 0; i < n; ++i) follow(cam, med[i]), fwd[i] = cam;
    cam = n ? med[n - 1] : 0.5;
    for (std::size_t i = n; i-- > 0;) follow(cam, med[i]), bwd[i] = cam;
    for (std::size_t i = 0; i < n; ++i) out[i] = (fwd[i] + bwd[i]) / 2.0;
    std::vector<double> avg(n);
    for (std::size_t i = 0; i < n; ++i) {
        double s = 0;
        int c = 0;
        for (std::size_t j = i >= 2 ? i - 2 : 0; j < std::min(n, i + 3); ++j) s += out[j], ++c;
        avg[i] = s / c;
    }
    return avg;
}
}  // namespace

ReframeResult autoReframe(const Project& project, const std::string& clipId, int targetW, int targetH,
                          const std::function<bool(double)>& progress) {
    ReframeResult r;
    const Clip* clip = project.findClip(clipId);
    const MediaAsset* asset = clip ? project.findAsset(clip->assetId) : nullptr;
    if (!clip || !asset || asset->width <= 0 || asset->height <= 0) {
        r.error = "Kein Videoclip";
        return r;
    }
    // Grundgröße (eingepasst) und Zoom, damit das Ziel ganz gefüllt ist
    const double fit = std::min(static_cast<double>(targetW) / asset->width, static_cast<double>(targetH) / asset->height);
    const double baseW = asset->width * fit, baseH = asset->height * fit;
    r.scale = std::max(targetW / baseW, targetH / baseH);
    const double maxX = (baseW * r.scale / targetW - 1.0) / 2.0, maxY = (baseH * r.scale / targetH - 1.0) / 2.0;

    // Den Clip allein in Quellformat (klein) rendern, ohne Transformation
    auto solo = std::make_shared<Project>(project);
    for (auto& tr : solo->tracks)
        tr.clips.erase(std::remove_if(tr.clips.begin(), tr.clips.end(), [&](const Clip& c) { return c.id != clipId; }), tr.clips.end());
    Clip* c = solo->findClip(clipId);
    for (const char* k : {"x", "y", "scale", "rotation"}) c->keys.erase(k);
    c->x = c->y = c->rotation = 0;
    c->scale = 1;
    c->mask.shape = MaskShape::None;
    c->fx.aiCutout = false;
    const int W = 256;
    const int H = std::max(16, static_cast<int>(std::lround(W * static_cast<double>(asset->height) / asset->width)));
    solo->width = W;
    solo->height = H;
    Compositor comp;
    comp.setProject(solo);
    const bool useAi = !cutoutModel().empty();
    r.usedAi = useAi;

    const TimeUs step = 250'000;
    std::vector<TimeUs> times;
    std::vector<double> us, vs;
    std::vector<std::uint8_t> rgba, prev;
    const TimeUs dur = clip->end() - clip->start;
    for (TimeUs t = 0; t < dur; t += step) {
        comp.render(clip->start + t, W, H, rgba);
        const Point p = subjectOf(rgba, prev, W, H, useAi);
        prev = rgba;
        times.push_back(t);
        us.push_back(p.u);
        vs.push_back(p.v);
        if (progress && !progress(static_cast<double>(t) / static_cast<double>(std::max<TimeUs>(1, dur)))) {
            r.cancelled = true;
            return r;
        }
    }
    if (times.empty()) {
        r.error = "Clip ist zu kurz";
        return r;
    }
    const auto su = smoothPath(us, 0.05), sv = smoothPath(vs, 0.05);
    for (std::size_t i = 0; i < times.size(); ++i) {
        // Motiv in die Bildmitte: x = −(u − 0,5) · Zoom · Grundbreite / Zielbreite
        const double x = std::clamp(-(su[i] - 0.5) * baseW * r.scale / targetW, -maxX, maxX);
        const double y = std::clamp(-(sv[i] - 0.5) * baseH * r.scale / targetH, -maxY, maxY);
        if (maxX > 1e-4) r.x.push_back({times[i], x});
        if (maxY > 1e-4) r.y.push_back({times[i], y});
    }
    // Gleiche Werte hintereinander zusammenfassen (weniger Keyframes)
    auto thin = [](std::vector<Keyframe>& k) {
        std::vector<Keyframe> o;
        for (std::size_t i = 0; i < k.size(); ++i) {
            const bool same = i > 0 && i + 1 < k.size() && std::abs(k[i].value - k[i - 1].value) < 1e-4 && std::abs(k[i + 1].value - k[i].value) < 1e-4;
            if (!same) o.push_back(k[i]);
        }
        k.swap(o);
    };
    thin(r.x);
    thin(r.y);
    return r;
}

}  // namespace loggx
