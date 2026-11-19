// Masken-Tracking: verfolgt den Bildinhalt unter der Maske von Bild zu Bild
// (Vorlagenvergleich auf Graustufen, grob → fein) und liefert Keyframes für
// die Maskenposition. Gerechnet wird auf dem zusammengesetzten Bild in
// kleiner Auflösung – so stimmen Position, Größe und Drehung des Clips.
#include <algorithm>
#include <cmath>
#include <limits>

#include "loggx/engine/Engine.h"

namespace loggx {

namespace {
struct Gray {
    int w = 0, h = 0;
    std::vector<float> px;
    float at(int x, int y) const { return px[static_cast<std::size_t>(y) * w + x]; }
};

Gray toGray(const std::vector<std::uint8_t>& rgba, int w, int h) {
    Gray g;
    g.w = w;
    g.h = h;
    g.px.resize(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < g.px.size(); ++i)
        g.px[i] = 0.299f * rgba[i * 4] + 0.587f * rgba[i * 4 + 1] + 0.114f * rgba[i * 4 + 2];
    return g;
}

// Mittelwertfreie Kreuzkorrelation (robust gegen Helligkeitsänderungen)
double ncc(const Gray& img, const std::vector<float>& tpl, int tw, int th, int x0, int y0, double tplMean, double tplNorm) {
    double sum = 0, sumSq = 0, cross = 0;
    const int n = tw * th;
    for (int y = 0; y < th; ++y) {
        const float* row = &img.px[static_cast<std::size_t>(y0 + y) * img.w + x0];
        const float* trow = &tpl[static_cast<std::size_t>(y) * tw];
        for (int x = 0; x < tw; ++x) {
            const double v = row[x];
            sum += v;
            sumSq += v * v;
            cross += v * (trow[x] - tplMean);
        }
    }
    const double var = sumSq - sum * sum / n;
    if (var <= 1e-6 || tplNorm <= 1e-6) return -1.0;
    return cross / std::sqrt(var * tplNorm);
}
}  // namespace

MaskTrackResult trackMask(const Project& project, const std::string& clipId, TimeUs from,
                          const std::function<bool(double)>& progress) {
    MaskTrackResult r;
    const Clip* clip = project.findClip(clipId);
    if (!clip || clip->mask.shape == MaskShape::None) {
        r.error = "Clip hat keine Maske";
        return r;
    }
    // Ohne Maske dieses Clips rendern, damit der ganze Inhalt sichtbar ist
    auto copy = std::make_shared<Project>(project);
    Clip* c = copy->findClip(clipId);
    c->mask.shape = MaskShape::None;
    Compositor comp;
    comp.setProject(copy);
    const int W = 480;
    const int H = std::max(16, static_cast<int>(std::lround(W * static_cast<double>(project.height) / std::max(1, project.width)))) & ~1;
    const TimeUs step = static_cast<TimeUs>(std::llround(1e6 / std::max(1.0, project.fps())));
    from = std::clamp(from, clip->start, clip->end() - 1);
    std::vector<std::uint8_t> rgba;

    // Vorlage: Inhalt unter der Maske (höchstens 40 % des Bildes, mindestens 12 px)
    double cx = clip->valueAt("maskX", from) * W, cy = clip->valueAt("maskY", from) * H;
    const int tw = std::clamp(static_cast<int>(clip->mask.w * W * 0.8), 12, static_cast<int>(W * 0.4));
    const int th = std::clamp(static_cast<int>(clip->mask.h * H * 0.8), 12, static_cast<int>(H * 0.4));
    comp.render(from, W, H, rgba);
    Gray g = toGray(rgba, W, H);
    auto cut = [&](const Gray& img, double ccx, double ccy, std::vector<float>& out) {
        const int x0 = std::clamp(static_cast<int>(std::lround(ccx - tw / 2.0)), 0, W - tw);
        const int y0 = std::clamp(static_cast<int>(std::lround(ccy - th / 2.0)), 0, H - th);
        out.resize(static_cast<std::size_t>(tw) * th);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) out[static_cast<std::size_t>(y) * tw + x] = img.at(x0 + x, y0 + y);
    };
    std::vector<float> tpl, fresh;
    cut(g, cx, cy, tpl);
    auto stats = [&](double& mean, double& norm) {
        mean = 0;
        for (float v : tpl) mean += v;
        mean /= static_cast<double>(tpl.size());
        norm = 0;
        for (float v : tpl) norm += (v - mean) * (v - mean);
    };
    double mean = 0, norm = 0;
    stats(mean, norm);
    if (norm < tpl.size() * 4.0) {   // Fläche ohne Struktur (Std.-Abw. < 2)
        r.error = "Unter der Maske ist zu wenig Struktur zum Verfolgen";
        return r;
    }
    r.x.push_back({from - clip->start, cx / W});
    r.y.push_back({from - clip->start, cy / H});
    double vx = 0, vy = 0;   // Bewegung des letzten Bildes (Vorhersage)
    const int radius = std::max(8, W / 12);
    const TimeUs total = clip->end() - from;
    for (TimeUs t = from + step; t < clip->end(); t += step) {
        comp.render(t, W, H, rgba);
        g = toGray(rgba, W, H);
        const double px = cx + vx, py = cy + vy;
        double best = -2, bx = px, by = py;
        auto test = [&](int ccx, int ccy) {
            const int x0 = ccx - tw / 2, y0 = ccy - th / 2;
            if (x0 < 0 || y0 < 0 || x0 + tw > W || y0 + th > H) return;
            const double s = ncc(g, tpl, tw, th, x0, y0, mean, norm);
            if (s > best) {
                best = s;
                bx = ccx;
                by = ccy;
            }
        };
        // grob (Schritt 3) im Suchfenster, dann fein um den Treffer
        for (int dy = -radius; dy <= radius; dy += 3)
            for (int dx = -radius; dx <= radius; dx += 3) test(static_cast<int>(std::lround(px)) + dx, static_cast<int>(std::lround(py)) + dy);
        const int fx = static_cast<int>(bx), fy = static_cast<int>(by);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) test(fx + dx, fy + dy);
        if (best < 0.35) {   // verloren (verdeckt, Schnitt): hier aufhören
            r.lost = true;
            break;
        }
        vx = bx - cx;
        vy = by - cy;
        cx = bx;
        cy = by;
        r.x.push_back({t - clip->start, cx / W});
        r.y.push_back({t - clip->start, cy / H});
        ++r.frames;
        // Vorlage langsam nachführen (Licht, leichte Drehung)
        cut(g, cx, cy, fresh);
        for (std::size_t i = 0; i < tpl.size(); ++i) tpl[i] = 0.85f * tpl[i] + 0.15f * fresh[i];
        stats(mean, norm);
        if (progress && !progress(static_cast<double>(t - from) / static_cast<double>(std::max<TimeUs>(1, total)))) {
            r.cancelled = true;
            break;
        }
    }
    // Ruckeln glätten (gleitender Mittelwert über 3 Bilder)
    auto smooth = [](std::vector<Keyframe>& k) {
        if (k.size() < 3) return;
        std::vector<Keyframe> o = k;
        for (std::size_t i = 1; i + 1 < k.size(); ++i) o[i].value = (k[i - 1].value + k[i].value + k[i + 1].value) / 3.0;
        k.swap(o);
    };
    smooth(r.x);
    smooth(r.y);
    return r;
}

}  // namespace loggx
