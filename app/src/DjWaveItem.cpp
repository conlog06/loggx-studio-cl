#include "DjWaveItem.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
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
    if (c) connect(c, &DjController::tick, this, [this] { update(); });
    emit changed();
    update();
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
    const auto& wave = track->wave;
    const double cols = static_cast<double>(wave.size());
    const double mid = h / 2;

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
    const double loopBeats = m_controller->loopBeats(m_deck);
    if (loopBeats > 0 && gridBpm > 0) {
        // Start = letzter Schlag vor der Position innerhalb des Loops (Anzeige genähert)
        const double len = loopBeats * beatLen;
        const double b = beatLen;
        double start = gridFirst + std::floor((pos - gridFirst) / b) * b;
        start = std::max(0.0, start - std::fmod(std::max(0.0, start - gridFirst), len));
        p->fillRect(QRectF(xOf(start), 0, xOf(start + len) - xOf(start), h), QColor(60, 220, 120, 45));
    }

    // Wellenform: je Pixelspalte Maximum der drei Bänder
    const int W = static_cast<int>(w);
    for (int x = 0; x < W; ++x) {
        const double ta = t0 + (t1 - t0) * x / w, tb = t0 + (t1 - t0) * (x + 1) / w;
        int ca = static_cast<int>(ta * loggx::DjTrack::kWaveRate), cb = static_cast<int>(std::ceil(tb * loggx::DjTrack::kWaveRate));
        if (cb <= 0 || ca >= cols) continue;
        ca = std::max(0, ca);
        cb = std::min(static_cast<int>(cols), std::max(cb, ca + 1));
        int lo = 0, mi = 0, hi = 0;
        for (int c = ca; c < cb; ++c) {
            const auto& v = wave[static_cast<std::size_t>(c)];
            lo = std::max(lo, int(v[1]));
            mi = std::max(mi, int(v[2]));
            hi = std::max(hi, int(v[3]));
        }
        const bool played = m_overview && ta < pos;
        const double s = (h / 2 - 1) / 255.0;
        auto bar = [&](int v, const QColor& c) {
            QColor col = c;
            if (played) col = col.darker(260);
            const double hh = v * s;
            p->fillRect(QRectF(x, mid - hh, 1, hh * 2), col);
        };
        // Reihenfolge: Höhen (außen hell), Mitten, Bass (innen kräftig)
        bar(std::min(255, std::max(lo, std::max(mi, hi))), kHigh);
        bar(std::min(255, std::max(lo, mi)), kMid);
        bar(lo, kLow);
    }

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
