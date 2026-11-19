#include "DjWaveItem.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QQuickWindow>
#include <cstring>
#include <QPainter>
#include <QWheelEvent>

#include "DjController.h"

namespace {
const QColor kLow(0x2f, 0x7b, 0xff);
const QColor kMid(0xff, 0xa0, 0x3c);
const QColor kHigh(0xf4, 0xf4, 0xf4);
const QColor kHot[8] = {QColor(0xff, 0x3b, 0x5c), QColor(0xff, 0x8c, 0x1a), QColor(0xff, 0xd4, 0x1a), QColor(0x3c, 0xd9, 0x6b),
                        QColor(0x2f, 0xc6, 0xff), QColor(0x5b, 0x6c, 0xff), QColor(0xb0, 0x5b, 0xff), QColor(0xff, 0x5b, 0xd6)};
}  // namespace

DjWaveItem::DjWaveItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setOpaquePainting(true);
}

void DjWaveItem::setController(DjController* c) {
    if (m_controller == c) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = c;
    if (c) {
        connect(c, &DjController::tick, this, &DjWaveItem::onTick);
        connect(c, &DjController::waveZoomChanged, this, [this] { update(); });
        connect(c, &DjController::settingsChanged, this, [this] { update(); });
    }
    emit changed();
    update();
}

std::uint64_t DjWaveItem::signature() const {
    // Alles, was das Bild verändert – Position auf Pixel gerundet
    if (!m_controller) return 0;
    const auto track = m_controller->track(m_deck);
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(track.get());
    auto mix = [&h](double v) {
        std::uint64_t b;
        std::memcpy(&b, &v, sizeof b);
        h ^= b + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    if (!track) return h;
    const double w = std::max(1.0, width());
    const double pos = m_controller->position(m_deck);
    const double secPerPx = m_overview ? std::max(0.001, track->duration()) / w : m_zoom / w;
    mix(std::floor(pos / secPerPx * 2.0));   // halbe Pixel
    mix(m_controller->cuePoint(m_deck));
    for (int i = 0; i < 8; ++i) mix(m_controller->hotCuePoint(m_deck, i));
    mix(m_controller->loopBeats(m_deck));
    mix(m_controller->mixer().gridBpm(m_deck));
    mix(m_controller->mixer().gridFirstBeat(m_deck));
    mix(m_zoom);
    mix(m_controller->waveStyle());
    mix(width());
    mix(height());
    return h;
}

void DjWaveItem::onTick() {
    if (!isVisible()) return;
    const std::uint64_t sig = signature();
    if (sig == m_lastSig) return;
    m_lastSig = sig;
    update();
}

// Schreibt die Wellenform spaltenweise direkt in die Pixel (statt tausender
// fillRect-Aufrufe): Bass innen kräftig, Mitten, Höhen außen hell.
void DjWaveItem::renderWave(QImage& img, double t0, double t1, double darkUntil) const {
    const auto track = m_controller->track(m_deck);
    const auto& wave = track->wave;
    const int W = img.width(), H = img.height();
    const int cols = static_cast<int>(wave.size());
    const double mid = H / 2.0;
    const double s = (H / 2.0 - 1) / 255.0;
    const int style = m_controller->waveStyle();
    QRgb cHigh, cMid, cLow;
    if (style == 1) {
        cHigh = qRgb(0x9f, 0xd4, 0xff); cMid = qRgb(0x2f, 0x8c, 0xff); cLow = qRgb(0x1a, 0x4f, 0xc8);
    } else if (style == 2) {
        cHigh = cMid = cLow = qRgb(0xd8, 0xdc, 0xe4);
    } else {
        cHigh = kHigh.rgb(); cMid = kMid.rgb(); cLow = kLow.rgb();
    }
    auto dark = [](QRgb c) { return qRgb(qRed(c) * 10 / 26, qGreen(c) * 10 / 26, qBlue(c) * 10 / 26); };
    const QRgb bg = qRgb(0x0a, 0x0b, 0x0e);
    img.fill(bg);
    const int bpl = static_cast<int>(img.bytesPerLine() / 4);
    QRgb* bits = reinterpret_cast<QRgb*>(img.bits());
    for (int x = 0; x < W; ++x) {
        const double ta = t0 + (t1 - t0) * x / W, tb = t0 + (t1 - t0) * (x + 1) / W;
        int ca = static_cast<int>(std::floor(ta * loggx::DjTrack::kWaveRate)), cb = static_cast<int>(std::ceil(tb * loggx::DjTrack::kWaveRate));
        if (cb <= 0 || ca >= cols) continue;
        ca = std::max(0, ca);
        cb = std::min(cols, std::max(cb, ca + 1));
        int lo = 0, mi = 0, hi = 0;
        for (int c = ca; c < cb; ++c) {
            const auto& v = wave[static_cast<std::size_t>(c)];
            lo = std::max(lo, int(v[1]));
            mi = std::max(mi, int(v[2]));
            hi = std::max(hi, int(v[3]));
        }
        const int all = std::min(255, std::max(lo, std::max(mi, hi)));
        const int lm = style == 2 ? all : std::min(255, std::max(lo, mi));
        const int l = style == 2 ? all : lo;
        const bool played = ta < darkUntil;
        const QRgb h1 = played ? dark(cHigh) : cHigh, m1 = played ? dark(cMid) : cMid, l1 = played ? dark(cLow) : cLow;
        const int eAll = static_cast<int>(all * s), eLm = static_cast<int>(lm * s), eL = static_cast<int>(l * s);
        const int y0 = std::max(0, static_cast<int>(mid) - eAll), y1 = std::min(H - 1, static_cast<int>(mid) + eAll);
        for (int y = y0; y <= y1; ++y) {
            const int d = std::abs(y - static_cast<int>(mid));
            bits[y * bpl + x] = d <= eL ? l1 : d <= eLm ? m1 : h1;
        }
    }
}

void DjWaveItem::paint(QPainter* p) {
    const double w = width(), h = height();
    p->fillRect(QRectF(0, 0, w, h), QColor(0x0a, 0x0b, 0x0e));
    if (!m_controller) return;
    const auto track = m_controller->track(m_deck);
    if (!track || track->wave.empty()) {
        p->setPen(QColor(0x5f, 0x67, 0x75));
        p->drawText(QRectF(0, 0, w, h), Qt::AlignCenter, tr("Kein Track geladen – aus der Bibliothek hierher ziehen oder doppelklicken"));
        return;
    }
    const double pos = m_controller->position(m_deck);
    const double dur = track->duration();

    // Zeit ↔ x
    double t0, t1;
    if (m_overview) {
        t0 = 0;
        t1 = std::max(0.001, dur);
    } else {
        t0 = pos - m_zoom / 2;
        t1 = pos + m_zoom / 2;
    }
    auto xOf = [&](double t) { return (t - t0) / (t1 - t0) * w; };
    // Beatgrid des Decks (kann von der Spur abweichen, z. B. nach BPM ×2)
    const double gridBpm = m_controller->mixer().gridBpm(m_deck);
    const double gridFirst = m_controller->mixer().gridFirstBeat(m_deck);
    const double beatLen = gridBpm > 0 ? 60.0 / gridBpm : 0.0;

    // Loop-Bereich
    QRectF loopRect;
    const double loopBeats = m_controller->loopBeats(m_deck);
    if (loopBeats > 0 && gridBpm > 0) {
        // Start = letzter Schlag vor der Position innerhalb des Loops (Anzeige genähert)
        const double len = loopBeats * beatLen;
        const double b = beatLen;
        double start = gridFirst + std::floor((pos - gridFirst) / b) * b;
        start = std::max(0.0, start - std::fmod(std::max(0.0, start - gridFirst), len));
        loopRect = QRectF(xOf(start), 0, xOf(start + len) - xOf(start), h);
    }

    // Wellenform (Übersicht zwischengespeichert, gezoomt direkt gerendert)
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize pxs(std::max(1, static_cast<int>(w * dpr)), std::max(1, static_cast<int>(h * dpr)));
    if (m_overview) {
        if (m_overviewTrack != track.get() || m_overviewImg.size() != pxs || m_overviewStyle != m_controller->waveStyle()) {
            m_overviewImg = QImage(pxs, QImage::Format_RGB32);
            m_overviewDark = QImage(pxs, QImage::Format_RGB32);
            renderWave(m_overviewImg, t0, t1, -1.0);
            renderWave(m_overviewDark, t0, t1, 1e12);
            m_overviewTrack = track.get();
            m_overviewStyle = m_controller->waveStyle();
        }
        const double split = std::clamp(xOf(pos), 0.0, w);
        p->drawImage(QRectF(split, 0, w - split, h), m_overviewImg, QRectF(split * dpr, 0, (w - split) * dpr, pxs.height()));
        p->drawImage(QRectF(0, 0, split, h), m_overviewDark, QRectF(0, 0, split * dpr, pxs.height()));
    } else {
        if (m_frame.size() != pxs) m_frame = QImage(pxs, QImage::Format_RGB32);
        renderWave(m_frame, t0, t1, -1.0);
        p->drawImage(QRectF(0, 0, w, h), m_frame);
    }
    // Loop-Bereich über der Wellenform
    if (loopRect.width() > 0) p->fillRect(loopRect, QColor(60, 220, 120, 55));

    // Beat-Raster (gezoomt): Schläge fein, Takte deutlich
    if (!m_overview && gridBpm > 0) {
        const double b = beatLen;
        const long long first = static_cast<long long>(std::floor((t0 - gridFirst) / b));
        for (long long k = first; ; ++k) {
            const double t = gridFirst + k * b;
            if (t > t1) break;
            if (t < 0) continue;
            const bool bar = k % 4 == 0;
            p->fillRect(QRectF(xOf(t), 0, 1, h), bar ? QColor(255, 80, 80, 170) : QColor(255, 255, 255, 60));
        }
    }

    // Cue und Hot Cues
    const double cue = m_controller->cuePoint(m_deck);
    p->fillRect(QRectF(xOf(cue) - 1, 0, 2, h), QColor(0xff, 0x9a, 0x2f));
    for (int i = 0; i < 8; ++i) {
        const double hc = m_controller->hotCuePoint(m_deck, i);
        if (hc < 0) continue;
        const double x = xOf(hc);
        p->fillRect(QRectF(x, 0, 1.5, h), kHot[i]);
        p->setBrush(kHot[i]);
        p->setPen(Qt::NoPen);
        p->drawRect(QRectF(x, 0, 12, 12));
        p->setPen(Qt::black);
        QFont f = p->font();
        f.setPixelSize(9);
        f.setBold(true);
        p->setFont(f);
        p->drawText(QRectF(x, 0, 12, 12), Qt::AlignCenter, QString(QChar('A' + i)));
    }

    // Abspielposition
    const double px = m_overview ? xOf(pos) : w / 2;
    p->fillRect(QRectF(px - 1, 0, 2, h), m_overview ? QColor(Qt::white) : QColor(0xff, 0x3b, 0x4f));
    if (!m_overview) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0xff, 0x3b, 0x4f));
        QPolygonF tri;
        tri << QPointF(px - 6, 0) << QPointF(px + 6, 0) << QPointF(px, 7);
        p->drawPolygon(tri);
    }
    // Rahmen in Deck-Farbe
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(m_accent, 1));
    p->drawRect(QRectF(0.5, 0.5, w - 1, h - 1));
}

void DjWaveItem::mousePressEvent(QMouseEvent* e) {
    if (!m_controller || !m_controller->track(m_deck)) return;
    if (m_overview) {
        m_controller->seekRelative(m_deck, e->position().x() / width());
    } else {
        // Ziehen im Zoom = Jog (vor/zurück schieben)
        m_dragging = true;
        m_dragX = e->position().x();
        m_dragPos = m_controller->position(m_deck);
    }
    e->accept();
}

void DjWaveItem::mouseMoveEvent(QMouseEvent* e) {
    if (!m_controller) return;
    if (m_overview) {
        m_controller->seekRelative(m_deck, e->position().x() / width());
    } else if (m_dragging) {
        const double dt = -(e->position().x() - m_dragX) / width() * m_zoom;
        m_controller->seek(m_deck, m_dragPos + dt);
    }
}

void DjWaveItem::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void DjWaveItem::wheelEvent(QWheelEvent* e) {
    if (m_overview) return;
    if (m_controller) m_controller->zoomWave(e->angleDelta().y() > 0 ? 1 : -1);
    else setZoom(std::clamp(m_zoom * (e->angleDelta().y() > 0 ? 0.8 : 1.25), 2.0, 32.0));
    e->accept();
}
