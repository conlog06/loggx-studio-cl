#include "Items.h"

#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

using namespace loggx;

namespace {

constexpr double kHeaderWidth = 124.0;
constexpr double kRulerHeight = 26.0;
constexpr double kTrackHeight = 52.0;
constexpr double kEdgeZone = 7.0;
constexpr double kSnapPixels = 8.0;

// Farben wie im QML-Theme (LoggXStyle/Theme.qml)
const QColor kBackground(0x0f, 0x11, 0x15);
const QColor kRuler(0x15, 0x18, 0x1e);
const QColor kRowA(0x13, 0x16, 0x1b);
const QColor kRowB(0x17, 0x1a, 0x20);
const QColor kLine(0x2a, 0x30, 0x3a);
const QColor kText(0xe8, 0xeb, 0xf1);
const QColor kTextDim(0x9a, 0xa2, 0xb1);
const QColor kVideoClip(0x4f, 0x7d, 0xf0);
const QColor kTitleClip(0xa0, 0x6b, 0xf0);
const QColor kAudioClip(0x2f, 0xb3, 0x83);
const QColor kMidiClip(0xf0, 0x92, 0x3b);
const QColor kPlayhead(0xff, 0x5a, 0x6a);
const QColor kAccent(0x5b, 0x8c, 0xff);

QString toQ(const std::string& s) { return QString::fromStdString(s); }

QColor trackColor(TrackKind k) {
    return k == TrackKind::Video ? kVideoClip : k == TrackKind::Audio ? kAudioClip : kMidiClip;
}

}  // namespace

// ------------------------------------------------------------ ViewerItem

ViewerItem::ViewerItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFillColor(Qt::black);
}

void ViewerItem::setController(AppController* controller) {
    if (controller == m_controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AppController::frameReady, this, [this](const QImage& image) {
            m_image = image;
            update();
        });
        reportSize();
    }
    emit controllerChanged();
}

void ViewerItem::paint(QPainter* painter) {
    if (m_image.isNull() || width() < 1 || height() < 1) return;
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const double scale = std::min(width() / m_image.width(), height() / m_image.height());
    double w = m_image.width() * scale, h = m_image.height() * scale;
    // Passt das Bild (fast) pixelgenau, ungeschärft und ohne Skalierung zeichnen.
    if (std::abs(w * dpr - m_image.width()) < 3 && std::abs(h * dpr - m_image.height()) < 3) {
        w = m_image.width() / dpr;
        h = m_image.height() / dpr;
    } else {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
    }
    const double x = std::round((width() - w) / 2 * dpr) / dpr, y = std::round((height() - h) / 2 * dpr) / dpr;
    painter->drawImage(QRectF(x, y, w, h), m_image);
}

void ViewerItem::reportSize() {
    if (!m_controller || !isVisible() || width() < 16 || height() < 16) return;
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    m_controller->setViewerSize(static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
}

void ViewerItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    reportSize();
}

void ViewerItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) reportSize();
}

// ---------------------------------------------------------- TimelineItem

TimelineItem::TimelineItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFillColor(kBackground);
    setAntialiasing(true);
}

void TimelineItem::setController(AppController* controller) {
    if (controller == m_controller) return;
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
        disconnect(&m_controller->mediaCache(), nullptr, this, nullptr);
    }
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AppController::projectChanged, this, [this] {
            setScrollY(m_scrollY);
            emit viewChanged();
            update();
        });
        connect(m_controller, &AppController::selectionChanged, this, [this] { update(); });
        connect(m_controller, &AppController::gridChanged, this, [this] { update(); });
        connect(m_controller, &AppController::positionChanged, this, [this] {
            followPlayhead();
            update();
        });
        connect(&m_controller->mediaCache(), &MediaCache::updated, this, [this] { update(); },
                Qt::QueuedConnection);
    }
    emit controllerChanged();
    emit viewChanged();
    update();
}

void TimelineItem::setPixelsPerSecond(double pps) {
    pps = std::clamp(pps, 2.0, 2000.0);
    if (qFuzzyCompare(pps, m_pps)) return;
    m_pps = pps;
    setScrollX(m_scrollX);
    emit viewChanged();
    update();
}

void TimelineItem::setScrollX(double x) {
    x = std::clamp(x, 0.0, std::max(0.0, contentWidth() - viewportWidth()));
    if (qFuzzyCompare(x + 1.0, m_scrollX + 1.0)) return;
    m_scrollX = x;
    emit viewChanged();
    update();
}

void TimelineItem::setScrollY(double y) {
    y = std::clamp(y, 0.0, std::max(0.0, contentHeight() - height()));
    if (qFuzzyCompare(y + 1.0, m_scrollY + 1.0)) return;
    m_scrollY = y;
    emit viewChanged();
    update();
}

double TimelineItem::viewportWidth() const { return std::max(1.0, width() - kHeaderWidth); }

double TimelineItem::contentWidth() const {
    const double seconds = m_controller ? m_controller->duration() : 0.0;
    // Hinter dem letzten Clip bleibt Platz zum Anfügen.
    return std::max(viewportWidth(), seconds * m_pps + viewportWidth() * 0.5);
}

double TimelineItem::contentHeight() const {
    const std::size_t tracks = m_controller ? m_controller->project().tracks.size() : 0;
    return kRulerHeight + static_cast<double>(tracks) * kTrackHeight + 8;
}

double TimelineItem::xOf(TimeUs t) const { return kHeaderWidth + t / 1e6 * m_pps - m_scrollX; }
double TimelineItem::yOfTrack(int index) const {
    return kRulerHeight + index * kTrackHeight - m_scrollY;
}

TimeUs TimelineItem::timeOf(double x) const {
    return static_cast<TimeUs>(std::max(0.0, (x - kHeaderWidth + m_scrollX) / m_pps) * 1e6);
}

double TimelineItem::timeAt(double x) const { return timeOf(x) / 1e6; }

int TimelineItem::trackIndexAt(double y) const {
    if (!m_controller || y < kRulerHeight) return -1;
    const int index = static_cast<int>((y - kRulerHeight + m_scrollY) / kTrackHeight);
    return index < static_cast<int>(m_controller->project().tracks.size()) ? index : -1;
}

void TimelineItem::zoomToFit() {
    if (!m_controller || m_controller->duration() <= 0) return;
    m_scrollX = 0;
    setPixelsPerSecond(viewportWidth() * 0.9 / m_controller->duration());
    emit viewChanged();
    update();
}

void TimelineItem::followPlayhead() {
    if (!m_controller || !m_controller->playing()) return;
    const double x = xOf(m_controller->positionUs());
    if (x > width() - 40 || x < kHeaderWidth)
        setScrollX(m_controller->position() * m_pps - viewportWidth() * 0.1);
}

void TimelineItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    emit viewChanged();
}

TimelineItem::Hit TimelineItem::hitTest(const QPointF& pos) const {
    Hit hit;
    hit.trackIndex = trackIndexAt(pos.y());
    if (hit.trackIndex < 0 || pos.x() < kHeaderWidth) return hit;
    const Track& track = m_controller->project().tracks[static_cast<std::size_t>(hit.trackIndex)];
    for (const Clip& c : track.clips) {
        const double x0 = xOf(c.start), x1 = xOf(c.end());
        if (pos.x() < x0 || pos.x() > x1) continue;
        hit.clipId = toQ(c.id);
        // Schmale Clips: Ränder dürfen die Mitte nicht ganz verdecken.
        const double edge = std::min(kEdgeZone, (x1 - x0) / 3.0);
        hit.zone = pos.x() - x0 <= edge ? Drag::TrimStart
                   : x1 - pos.x() <= edge ? Drag::TrimEnd
                                          : Drag::Move;
        break;
    }
    return hit;
}

TimeUs TimelineItem::snap(TimeUs t, const QString& ignoreClip, bool* snapped) const {
    const TimeUs threshold = static_cast<TimeUs>(kSnapPixels / m_pps * 1e6);
    TimeUs best = t;
    TimeUs bestDist = threshold + 1;
    auto consider = [&](TimeUs candidate) {
        const TimeUs dist = std::llabs(candidate - t);
        if (dist < bestDist) {
            bestDist = dist;
            best = candidate;
        }
    };
    consider(0);
    consider(m_controller->positionUs());
    if (m_controller->beatGrid()) {
        const Project& project = m_controller->project();
        consider(project.nearestBeat(t));
    }
    for (const Track& track : m_controller->project().tracks)
        for (const Clip& c : track.clips) {
            if (toQ(c.id) == ignoreClip) continue;
            consider(c.start);
            consider(c.end());
        }
    if (snapped) *snapped = bestDist <= threshold;
    return bestDist <= threshold ? best : t;
}

// --------------------------------------------------------------- Maus

void TimelineItem::mousePressEvent(QMouseEvent* event) {
    if (!m_controller) return;
    forceActiveFocus();
    const QPointF pos = event->position();

    if (event->button() == Qt::RightButton) {
        // Kontextmenü: Clip darunter auswählen, Menü in QML anzeigen
        const int index = pos.y() >= kRulerHeight ? trackIndexAt(pos.y()) : -1;
        const bool header = pos.x() < kHeaderWidth;
        QString clipId;
        if (!header && pos.y() >= kRulerHeight) {
            clipId = hitTest(pos).clipId;
            if (!clipId.isEmpty()) m_controller->setSelectedClipId(clipId);
        }
        const double seconds = header ? -1.0 : static_cast<double>(timeOf(pos.x())) / 1e6;
        emit contextMenuRequested(pos.x(), pos.y(), index, header, clipId, seconds);
        return;
    }

    if (pos.x() < kHeaderWidth) {
        // Klick auf den Spurkopf schaltet die Spur stumm bzw. unsichtbar.
        const int index = trackIndexAt(pos.y());
        if (index >= 0) m_controller->toggleTrackMute(index);
        return;
    }
    if (pos.y() < kRulerHeight) {
        m_drag = Drag::Scrub;
        m_controller->pause();
        m_controller->seekUs(timeOf(pos.x()));
        return;
    }

    const Hit hit = hitTest(pos);
    m_controller->setSelectedClipId(hit.clipId);
    if (hit.clipId.isEmpty()) {
        // Klick ins Leere setzt den Abspielkopf.
        m_drag = Drag::Scrub;
        m_controller->seekUs(timeOf(pos.x()));
        return;
    }
    const Clip* clip = m_controller->project().findClip(hit.clipId.toStdString());
    m_drag = hit.zone;
    m_dragClip = hit.clipId;
    m_grabOffset = timeOf(pos.x()) - clip->start;
    m_controller->beginInteractive();
}

void TimelineItem::mouseMoveEvent(QMouseEvent* event) {
    if (!m_controller || m_drag == Drag::None) return;
    const QPointF pos = event->position();
    const TimeUs t = timeOf(pos.x());

    if (m_drag == Drag::Scrub) {
        m_controller->seekUs(t);
        return;
    }
    const Project& project = m_controller->project();
    const Track* track = nullptr;
    const Clip* clip = project.findClip(m_dragClip.toStdString(), &track);
    if (!clip) return;

    if (m_drag == Drag::TrimStart) {
        m_controller->trimStartLive(m_dragClip, snap(t, m_dragClip));
    } else if (m_drag == Drag::TrimEnd) {
        m_controller->trimEndLive(m_dragClip, snap(t, m_dragClip));
    } else {
        // Anfang oder Ende des Clips rastet an Nachbarn und Abspielkopf ein.
        TimeUs start = std::max<TimeUs>(0, t - m_grabOffset);
        bool snappedStart = false, snappedEnd = false;
        const TimeUs byStart = snap(start, m_dragClip, &snappedStart);
        const TimeUs byEnd = snap(start + clip->duration, m_dragClip, &snappedEnd) - clip->duration;
        if (snappedStart)
            start = byStart;
        else if (snappedEnd)
            start = std::max<TimeUs>(0, byEnd);

        QString target = toQ(track->id);
        const int row = trackIndexAt(pos.y());
        if (row >= 0) {
            const Track& candidate = project.tracks[static_cast<std::size_t>(row)];
            if (candidate.kind == track->kind) target = toQ(candidate.id);
        }
        const QString own = toQ(track->id);
        if (!m_controller->moveClipLive(m_dragClip, target, start) && target != own)
            m_controller->moveClipLive(m_dragClip, own, start);
    }
}

void TimelineItem::mouseReleaseEvent(QMouseEvent*) {
    if (m_controller && m_drag != Drag::None && m_drag != Drag::Scrub)
        m_controller->endInteractive();
    m_drag = Drag::None;
    m_dragClip.clear();
}

void TimelineItem::mouseDoubleClickEvent(QMouseEvent* event) { mousePressEvent(event); }

void TimelineItem::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom um die Mausposition herum.
        const double x = event->position().x();
        const double seconds = (x - kHeaderWidth + m_scrollX) / m_pps;
        const double factor = std::pow(1.0015, delta.y());
        const double pps = std::clamp(m_pps * factor, 2.0, 2000.0);
        m_pps = pps;
        m_scrollX = std::clamp(seconds * pps - (x - kHeaderWidth), 0.0,
                               std::max(0.0, contentWidth() - viewportWidth()));
        emit viewChanged();
        update();
    } else if (delta.x() != 0 || (event->modifiers() & Qt::ShiftModifier)) {
        setScrollX(m_scrollX - (delta.x() != 0 ? delta.x() : delta.y()));
    } else if (contentHeight() > height()) {
        setScrollY(m_scrollY - delta.y() * 0.5);
    } else {
        setScrollX(m_scrollX - delta.y());
    }
    event->accept();
}

void TimelineItem::hoverMoveEvent(QHoverEvent* event) {
    if (!m_controller) return;
    const QPointF pos = event->position();
    if (pos.y() < kRulerHeight || pos.x() < kHeaderWidth) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const Hit hit = hitTest(pos);
    setCursor(hit.zone == Drag::TrimStart || hit.zone == Drag::TrimEnd ? Qt::SizeHorCursor
              : hit.zone == Drag::Move ? Qt::OpenHandCursor
                                       : Qt::ArrowCursor);
}

// ------------------------------------------------------------ Zeichnen

void TimelineItem::paintClipContent(QPainter* p, const Track& track, const Clip& c,
                                    const QRectF& rect, const QColor& base) {
    const Project& project = m_controller->project();
    const QRectF inner = rect.adjusted(1, 1, -1, -1);
    p->save();
    p->setClipRect(inner.intersected(QRectF(kHeaderWidth, 0, width() - kHeaderWidth, height())));

    if (c.kind == ClipKind::Media) {
        const MediaAsset* asset = project.findAsset(c.assetId);
        const auto entry = asset ? m_controller->mediaCache().entry(asset->path) : nullptr;
        if (entry && track.kind == TrackKind::Video && !entry->thumbs.empty()) {
            // Vorschaubilder nebeneinander, jeweils das Bild an dieser Stelle.
            const QImage& first = entry->thumbs.front().second;
            const double th = inner.height();
            const double tw = th * first.width() / std::max(1, first.height());
            const double startX = std::max(inner.left(), kHeaderWidth - tw);
            for (double x = inner.left() + std::floor((startX - inner.left()) / tw) * tw;
                 x < std::min(inner.right(), width()); x += tw) {
                const TimeUs src = c.sourceTime(timeOf(x + tw / 2));
                auto it = std::upper_bound(entry->thumbs.begin(), entry->thumbs.end(), src,
                                           [](TimeUs v, const auto& e) { return v < e.first; });
                if (it != entry->thumbs.begin()) --it;
                p->setOpacity(0.75);
                p->drawImage(QRectF(x, inner.top(), tw, th), it->second);
            }
            p->setOpacity(1.0);
        } else if (entry && !entry->peaks.empty()) {
            // Wellenform
            const double mid = inner.center().y() + 6;
            const double amp = inner.height() * 0.4;
            QColor wave = base.lighter(170);
            wave.setAlpha(200);
            p->setPen(QPen(wave, 1));
            const double gain = std::min(2.0, c.gain);
            const double x0 = std::max(inner.left(), kHeaderWidth);
            const double x1 = std::min(inner.right(), width());
            for (double x = x0; x < x1; x += 1.0) {
                const TimeUs a = c.sourceTime(timeOf(x));
                const TimeUs b = c.sourceTime(timeOf(x + 1.0));
                std::size_t i0 = static_cast<std::size_t>(std::max<TimeUs>(0, a) / entry->peakStep);
                std::size_t i1 = static_cast<std::size_t>(std::max<TimeUs>(0, b) / entry->peakStep) + 1;
                i1 = std::min(i1, entry->peaks.size());
                float pk = 0.0f;
                for (std::size_t i = i0; i < i1; ++i) pk = std::max(pk, entry->peaks[i]);
                const double hgt = std::min(1.0, pk * gain) * amp;
                if (hgt > 0.3) p->drawLine(QPointF(x, mid - hgt), QPointF(x, mid + hgt));
            }
        }
    } else if (c.kind == ClipKind::Midi && !c.notes.empty()) {
        // Noten im Kleinformat
        int lo = 127, hi = 0;
        for (const Note& n : c.notes) {
            lo = std::min(lo, n.pitch);
            hi = std::max(hi, n.pitch);
        }
        lo -= 2;
        hi += 2;
        const double top = inner.top() + 18, bottom = inner.bottom() - 4;
        const double rowH = std::max(1.5, (bottom - top) / std::max(1, hi - lo));
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 190));
        for (const Note& n : c.notes) {
            if (n.start + n.length <= c.in || n.start >= c.in + c.duration) continue;
            const double x = xOf(c.start + (n.start - c.in));
            const double xe = xOf(c.start + (n.start + n.length - c.in));
            const double y = bottom - (n.pitch - lo) * (bottom - top) / std::max(1, hi - lo);
            p->drawRect(QRectF(x, y - rowH / 2, std::max(1.5, xe - x), std::max(1.5, rowH * 0.8)));
        }
    }

    // Blenden als dunkle Dreiecke
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 110));
    if (c.fadeIn > 0) {
        const double fx = xOf(c.start + c.fadeIn);
        QPainterPath tri;
        tri.moveTo(inner.left(), inner.top());
        tri.lineTo(fx, inner.top());
        tri.lineTo(inner.left(), inner.bottom());
        tri.closeSubpath();
        p->drawPath(tri);
    }
    if (c.fadeOut > 0) {
        const double fx = xOf(c.end() - c.fadeOut);
        QPainterPath tri;
        tri.moveTo(inner.right(), inner.top());
        tri.lineTo(fx, inner.top());
        tri.lineTo(inner.right(), inner.bottom());
        tri.closeSubpath();
        p->drawPath(tri);
    }

    // Beschriftung auf dunklem Band
    p->fillRect(QRectF(inner.left(), inner.top(), inner.width(), 17), QColor(0, 0, 0, 70));
    QString label;
    if (c.kind == ClipKind::Media) {
        const MediaAsset* asset = project.findAsset(c.assetId);
        label = asset ? toQ(asset->name) : QStringLiteral("?");
    } else if (c.kind == ClipKind::Title) {
        label = QStringLiteral("T  ") + toQ(c.title.text).section(QLatin1Char('\n'), 0, 0);
    } else {
        label = QStringLiteral("MIDI · %1").arg(c.notes.size());
    }
    QStringList badges;
    if (std::abs(c.speed - 1.0) > 1e-6) badges << QStringLiteral("%1×").arg(QString::number(c.speed, 'g', 3));
    if (std::abs(c.pitch) > 1e-6)
        badges << QStringLiteral("%1%2 HT").arg(c.pitch > 0 ? QStringLiteral("+") : QString()).arg(QString::number(c.pitch, 'g', 3));
    if (c.mask.shape != MaskShape::None) badges << QObject::tr("Maske");
    if (!c.color.isNeutral()) badges << QObject::tr("Farbe");
    if (!c.fx.isNeutral()) badges << QObject::tr("FX");
    if (c.hasTransform()) badges << QObject::tr("Transform");
    if (!badges.isEmpty()) label += QStringLiteral("   ") + badges.join(QStringLiteral(" · "));
    p->setPen(Qt::white);
    p->drawText(QPointF(std::max(inner.left(), kHeaderWidth) + 6, inner.top() + 13), label);

    // Keyframes als Rauten am unteren Rand
    if (c.hasKeys()) {
        p->setPen(QPen(QColor(0, 0, 0, 160), 1));
        p->setBrush(QColor(0xff, 0xd5, 0x4f));
        for (const auto& [name, list] : c.keys)
            for (const Keyframe& k : list) {
                const double x = xOf(c.start + k.t);
                const double y = inner.bottom() - 6;
                QPainterPath d;
                d.moveTo(x, y - 4);
                d.lineTo(x + 4, y);
                d.lineTo(x, y + 4);
                d.lineTo(x - 4, y);
                d.closeSubpath();
                p->drawPath(d);
            }
    }
    p->restore();
}

void TimelineItem::paint(QPainter* p) {
    if (!m_controller) return;
    const Project& project = m_controller->project();
    const double w = width(), h = height();
    QFont font = p->font();
    font.setPixelSize(11);
    p->setFont(font);

    // Spuren
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const double y = yOfTrack(static_cast<int>(i));
        p->fillRect(QRectF(0, y, w, kTrackHeight), i % 2 ? kRowB : kRowA);
        p->fillRect(QRectF(0, y + kTrackHeight - 1, w, 1), kLine);
    }

    // Raster-Linien (im Lineal-Schritt)
    static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * m_pps >= 72.0) {
            step = s;
            break;
        }
    p->setClipRect(QRectF(kHeaderWidth, kRulerHeight, w - kHeaderWidth, h));
    const double first = std::floor(m_scrollX / m_pps / step) * step;
    const bool beats = m_controller->beatGrid();
    // Beat-Raster: Takte kräftig, Schläge zart; zu dichte Schläge entfallen.
    const double beatSec = project.beatDuration() / 1e6;
    const double offsetSec = project.beatOffset / 1e6;
    const int perBar = std::max(1, project.beatsPerBar);
    int beatStep = 1;
    while (beats && beatSec * beatStep * m_pps < 10.0) beatStep *= 2;
    auto forBeats = [&](auto&& fn) {
        if (!beats || beatSec <= 0) return;
        long long b0 = static_cast<long long>(std::floor((m_scrollX / m_pps - offsetSec) / beatSec));
        b0 -= ((b0 % beatStep) + beatStep) % beatStep;
        for (long long b = std::max(0LL, b0);; b += beatStep) {
            const double sec = offsetSec + static_cast<double>(b) * beatSec;
            const double x = kHeaderWidth + sec * m_pps - m_scrollX;
            if (x > w) break;
            fn(b, x);
        }
    };
    if (beats) {
        forBeats([&](long long b, double x) {
            const bool bar = b % perBar == 0;
            p->fillRect(QRectF(x, kRulerHeight, 1, h - kRulerHeight), bar ? QColor(255, 255, 255, 34) : QColor(255, 255, 255, 12));
        });
    } else {
        for (double s = first; s * m_pps - m_scrollX < w; s += step) {
            const double x = kHeaderWidth + s * m_pps - m_scrollX;
            p->fillRect(QRectF(x, kRulerHeight, 1, h - kRulerHeight), QColor(255, 255, 255, 10));
        }
    }

    // Clips
    const QString selected = m_controller->selectedClipId();
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& track = project.tracks[i];
        const double y = yOfTrack(static_cast<int>(i)) + 4;
        if (y > h || y + kTrackHeight < kRulerHeight) continue;
        for (const Clip& c : track.clips) {
            const double x0 = xOf(c.start), x1 = xOf(c.end());
            if (x1 < kHeaderWidth || x0 > w) continue;
            const QRectF rect(x0, y, std::max(2.0, x1 - x0 - 1), kTrackHeight - 9);
            const bool isSelected = toQ(c.id) == selected;
            QColor fill = c.kind == ClipKind::Title ? kTitleClip
                          : c.kind == ClipKind::Midi ? kMidiClip
                          : trackColor(track.kind);
            if (track.muted) fill = fill.darker(220);
            QPainterPath path;
            path.addRoundedRect(rect, 5, 5);
            p->fillPath(path, isSelected ? fill.lighter(118) : fill.darker(115));
            if (rect.width() > 14) paintClipContent(p, track, c, rect, fill);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(isSelected ? QColor(Qt::white) : fill.darker(150), isSelected ? 2 : 1));
            p->drawPath(path);
            // Übergang zum vorherigen Clip: Fliege über der Schnittkante
            if (c.transition != TransitionType::None && c.transitionDuration > 0) {
                const double half = std::max(5.0, c.transitionDuration / 2e6 * m_pps);
                const QRectF tr(x0 - half, rect.top() + 2, half * 2, rect.height() - 4);
                QPainterPath bow;
                bow.moveTo(tr.left(), tr.top());
                bow.lineTo(tr.right(), tr.bottom());
                bow.lineTo(tr.right(), tr.top());
                bow.lineTo(tr.left(), tr.bottom());
                bow.closeSubpath();
                p->fillRect(tr, QColor(0, 0, 0, 90));
                p->setPen(QPen(QColor(255, 255, 255, 200), 1.2));
                p->setBrush(QColor(255, 255, 255, 40));
                p->drawPath(bow);
                p->setBrush(Qt::NoBrush);
            }
        }
    }
    p->setClipping(false);

    // Lineal
    p->fillRect(QRectF(0, 0, w, kRulerHeight), kRuler);
    p->setClipRect(QRectF(kHeaderWidth, 0, w - kHeaderWidth, kRulerHeight));
    if (beats) {
        // Takt-Lineal: 1, 2, 3 … mit Schlag-Strichen
        int barStep = 1;
        while (beatSec * perBar * barStep * m_pps < 34.0) barStep *= 2;
        forBeats([&](long long b, double x) {
            const bool bar = b % perBar == 0;
            p->fillRect(QRectF(x, kRulerHeight - (bar ? 10 : 4), 1, bar ? 10 : 4), bar ? kTextDim : kLine.lighter(140));
            const long long barNo = b / perBar;
            if (bar && barNo % barStep == 0) {
                p->setPen(kText);
                p->drawText(QPointF(x + 4, 14), QString::number(barNo + 1));
            }
        });
        p->fillRect(QRectF(kHeaderWidth, kRulerHeight - 1, w, 1), kLine);
    } else for (double s = first; s * m_pps - m_scrollX < w; s += step) {
        const double x = kHeaderWidth + s * m_pps - m_scrollX;
        p->fillRect(QRectF(x, kRulerHeight - 8, 1, 8), kTextDim);
        const int total = static_cast<int>(std::floor(s + 1e-6));
        QString label = QStringLiteral("%1:%2")
                            .arg(total / 60, 2, 10, QLatin1Char('0'))
                            .arg(total % 60, 2, 10, QLatin1Char('0'));
        if (step < 1.0)
            label += QStringLiteral(".%1").arg(
                static_cast<int>(std::lround((s - total) * 100)), 2, 10, QLatin1Char('0'));
        p->setPen(kTextDim);
        p->drawText(QPointF(x + 4, 13), label);
    }
    p->setClipping(false);

    // Abspielkopf
    const double px = xOf(m_controller->positionUs());
    if (px >= kHeaderWidth - 1) {
        p->fillRect(QRectF(px - 0.75, 0, 1.5, h), kPlayhead);
        QPainterPath head;
        head.moveTo(px - 6, 0);
        head.lineTo(px + 6, 0);
        head.lineTo(px + 6, 8);
        head.lineTo(px, 15);
        head.lineTo(px - 6, 8);
        head.closeSubpath();
        p->fillPath(head, kPlayhead);
    }

    // Spurköpfe (über den Clips, damit nichts hineinragt)
    p->fillRect(QRectF(0, 0, kHeaderWidth, h), kRuler);
    p->fillRect(QRectF(kHeaderWidth - 1, 0, 1, h), kLine);
    p->setClipRect(QRectF(0, kRulerHeight, kHeaderWidth, h));
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& track = project.tracks[i];
        const double y = yOfTrack(static_cast<int>(i));
        p->fillRect(QRectF(0, y + kTrackHeight - 1, kHeaderWidth, 1), kLine);
        p->fillRect(QRectF(0, y, 3, kTrackHeight - 1), trackColor(track.kind));
        p->setPen(track.muted ? kTextDim : kText);
        p->drawText(QPointF(12, y + 21), toQ(track.name));
        QString state;
        if (track.kind == TrackKind::Video)
            state = track.muted ? tr("ausgeblendet") : tr("sichtbar");
        else if (track.kind == TrackKind::Instrument)
            state = track.muted ? tr("stumm") : QString::fromUtf8(instrumentName(track.instrument));
        else
            state = track.muted ? tr("stumm") : tr("an");
        if (track.solo) state += QStringLiteral(" · Solo");
        p->setPen(track.muted ? kPlayhead : kTextDim);
        p->drawText(QPointF(12, y + 37), state);
    }
    p->setClipping(false);
    p->fillRect(QRectF(0, 0, kHeaderWidth, kRulerHeight), kRuler);
}

// ---------------------------------------------------------- PianoRollItem

namespace {
constexpr double kKeyWidth = 54.0;
constexpr double kRollRuler = 22.0;
constexpr double kRowH = 13.0;
bool isBlack(int pitch) {
    const int n = pitch % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}
QString noteName(int pitch) {
    static const char* names[] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "H"};
    return QString::fromUtf8(names[pitch % 12]) + QString::number(pitch / 12 - 1);
}
}  // namespace

PianoRollItem::PianoRollItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFillColor(kBackground);
    setAntialiasing(true);
}

void PianoRollItem::setController(AppController* controller) {
    if (controller == m_controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        auto refresh = [this] {
            ensureView();
            emit viewChanged();
            emit noteSelectionChanged();
            update();
        };
        connect(m_controller, &AppController::projectChanged, this, refresh);
        connect(m_controller, &AppController::selectionChanged, this, refresh);
        connect(m_controller, &AppController::positionChanged, this, [this] { update(); });
    }
    emit controllerChanged();
    update();
}

void PianoRollItem::setGridDivision(int d) {
    d = std::clamp(d, 1, 16);
    if (d == m_division) return;
    m_division = d;
    emit viewChanged();
    update();
}

const Clip* PianoRollItem::clip() const {
    if (!m_controller || m_controller->selectedClipId().isEmpty()) return nullptr;
    const Clip* c = m_controller->project().findClip(m_controller->selectedClipId().toStdString());
    return c && c->kind == ClipKind::Midi ? c : nullptr;
}

double PianoRollItem::selectedVelocity() const {
    const Clip* c = clip();
    if (!c || m_selectedNote < 0 || m_selectedNote >= static_cast<int>(c->notes.size())) return 0.8;
    return c->notes[static_cast<std::size_t>(m_selectedNote)].velocity;
}

void PianoRollItem::setSelectedVelocity(double v) {
    const Clip* c = clip();
    if (!c || m_selectedNote < 0 || m_selectedNote >= static_cast<int>(c->notes.size())) return;
    Note n = c->notes[static_cast<std::size_t>(m_selectedNote)];
    n.velocity = v;
    m_controller->updateNote(m_controller->selectedClipId(), m_selectedNote, n);
}

void PianoRollItem::deleteSelectedNote() {
    if (!clip() || m_selectedNote < 0) return;
    m_controller->removeNote(m_controller->selectedClipId(), m_selectedNote);
    m_selectedNote = -1;
    emit noteSelectionChanged();
}

void PianoRollItem::ensureView() {
    const Clip* c = clip();
    const QString id = c ? QString::fromStdString(c->id) : QString();
    if (id == m_viewClip) {
        if (c && m_selectedNote >= static_cast<int>(c->notes.size())) m_selectedNote = -1;
        return;
    }
    m_viewClip = id;
    m_selectedNote = -1;
    if (!c) return;
    zoomToFit();
    // Senkrecht auf die Noten (oder C4) ausrichten.
    int lo = 60, hi = 72;
    if (!c->notes.empty()) {
        lo = 127;
        hi = 0;
        for (const Note& n : c->notes) {
            lo = std::min(lo, n.pitch);
            hi = std::max(hi, n.pitch);
        }
    }
    const double center = (lo + hi) / 2.0;
    const double visible = height() - kRollRuler;
    m_scrollY = std::clamp((127.0 - center) * kRowH - visible / 2.0, 0.0,
                           std::max(0.0, 128 * kRowH - visible));
}

void PianoRollItem::zoomToFit() {
    const Clip* c = clip();
    if (!c) return;
    m_pps = std::max(10.0, (width() - kKeyWidth - 10) / (c->duration / 1e6));
    m_scrollX = 0;
    update();
}

void PianoRollItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (oldGeometry.width() < 1 && newGeometry.width() > 1) {
        m_viewClip.clear();
        ensureView();
    }
}

double PianoRollItem::xOf(TimeUs contentTime) const {
    const Clip* c = clip();
    const TimeUs in = c ? c->in : 0;
    return kKeyWidth + (contentTime - in) / 1e6 * m_pps - m_scrollX;
}

TimeUs PianoRollItem::timeOf(double x) const {
    const Clip* c = clip();
    const TimeUs in = c ? c->in : 0;
    return in + static_cast<TimeUs>((x - kKeyWidth + m_scrollX) / m_pps * 1e6);
}

double PianoRollItem::yOf(int pitch) const { return kRollRuler + (127 - pitch) * kRowH - m_scrollY; }

int PianoRollItem::pitchAt(double y) const {
    return std::clamp(127 - static_cast<int>(std::floor((y - kRollRuler + m_scrollY) / kRowH)), 0, 127);
}

TimeUs PianoRollItem::gridStep() const {
    const TimeUs beat = m_controller ? m_controller->project().beatDuration() : 500'000;
    return std::max<TimeUs>(1000, beat / m_division);
}

int PianoRollItem::noteAt(const QPointF& pos, bool* onEdge) const {
    const Clip* c = clip();
    if (!c) return -1;
    for (int i = static_cast<int>(c->notes.size()) - 1; i >= 0; --i) {
        const Note& n = c->notes[static_cast<std::size_t>(i)];
        const double x0 = xOf(n.start), x1 = xOf(n.start + n.length), y = yOf(n.pitch);
        if (pos.x() >= x0 && pos.x() <= x1 && pos.y() >= y && pos.y() < y + kRowH) {
            if (onEdge) *onEdge = x1 - pos.x() <= std::min(7.0, (x1 - x0) / 3.0);
            return i;
        }
    }
    return -1;
}

void PianoRollItem::mousePressEvent(QMouseEvent* event) {
    const Clip* c = clip();
    if (!c) return;
    forceActiveFocus();
    const QPointF pos = event->position();
    if (pos.x() < kKeyWidth || pos.y() < kRollRuler) return;
    bool edge = false;
    const int hit = noteAt(pos, &edge);
    const QString clipId = m_controller->selectedClipId();

    if (event->button() == Qt::RightButton) {
        if (hit >= 0) {
            m_controller->removeNote(clipId, hit);
            m_selectedNote = -1;
            emit noteSelectionChanged();
        }
        return;
    }
    m_controller->beginInteractive();
    if (hit >= 0) {
        m_selectedNote = hit;
        m_grabNote = c->notes[static_cast<std::size_t>(hit)];
        m_drag = edge ? Drag::Resize : Drag::Move;
    } else {
        // Neue Note am Raster, danach ziehen = Länge.
        const TimeUs step = gridStep();
        const TimeUs t = timeOf(pos.x()) / step * step;
        if (t < c->in || t >= c->in + c->duration) {
            m_controller->endInteractive();
            return;
        }
        Note n;
        n.pitch = pitchAt(pos.y());
        n.start = t;
        n.length = m_lastLength > 0 ? m_lastLength : m_controller->project().beatDuration();
        n.velocity = 0.8;
        m_selectedNote = m_controller->addNote(clipId, n);
        m_grabNote = n;
        m_drag = Drag::Resize;
    }
    m_grabTime = timeOf(pos.x());
    m_grabPitch = pitchAt(pos.y());
    emit noteSelectionChanged();
    update();
}

void PianoRollItem::mouseMoveEvent(QMouseEvent* event) {
    const Clip* c = clip();
    if (!c || m_drag == Drag::None || m_selectedNote < 0) return;
    const QPointF pos = event->position();
    const TimeUs step = gridStep();
    Note n = m_grabNote;
    if (m_drag == Drag::Move) {
        const TimeUs delta = timeOf(pos.x()) - m_grabTime;
        n.start = std::max<TimeUs>(0, (m_grabNote.start + delta + step / 2) / step * step);
        n.pitch = std::clamp(m_grabNote.pitch + pitchAt(pos.y()) - m_grabPitch, 0, 127);
    } else {
        const TimeUs end = (timeOf(pos.x()) + step / 2) / step * step;
        n.length = std::max(step, end - n.start);
    }
    m_controller->updateNote(m_controller->selectedClipId(), m_selectedNote, n);
}

void PianoRollItem::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag != Drag::None && m_controller) {
        const Clip* c = clip();
        if (c && m_selectedNote >= 0 && m_selectedNote < static_cast<int>(c->notes.size()))
            m_lastLength = c->notes[static_cast<std::size_t>(m_selectedNote)].length;
        m_controller->endInteractive();
    }
    m_drag = Drag::None;
}

void PianoRollItem::mouseDoubleClickEvent(QMouseEvent* event) {
    // Doppelklick auf eine Note löscht sie.
    if (!clip()) return;
    const int hit = noteAt(event->position(), nullptr);
    if (hit >= 0) {
        m_controller->removeNote(m_controller->selectedClipId(), hit);
        m_selectedNote = -1;
        emit noteSelectionChanged();
    }
}

void PianoRollItem::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        const double x = event->position().x();
        const double seconds = (x - kKeyWidth + m_scrollX) / m_pps;
        m_pps = std::clamp(m_pps * std::pow(1.0015, delta.y()), 10.0, 3000.0);
        m_scrollX = std::max(0.0, seconds * m_pps - (x - kKeyWidth));
    } else if (delta.x() != 0 || (event->modifiers() & Qt::ShiftModifier)) {
        m_scrollX = std::max(0.0, m_scrollX - (delta.x() != 0 ? delta.x() : delta.y()));
    } else {
        const double visible = height() - kRollRuler;
        m_scrollY = std::clamp(m_scrollY - delta.y() * 0.6, 0.0, std::max(0.0, 128 * kRowH - visible));
    }
    update();
    event->accept();
}

void PianoRollItem::hoverMoveEvent(QHoverEvent* event) {
    bool edge = false;
    const int hit = clip() ? noteAt(event->position(), &edge) : -1;
    setCursor(hit < 0 ? Qt::CrossCursor : edge ? Qt::SizeHorCursor : Qt::OpenHandCursor);
}

void PianoRollItem::paint(QPainter* p) {
    const Clip* c = clip();
    const double w = width(), h = height();
    QFont font = p->font();
    font.setPixelSize(10);
    p->setFont(font);
    if (!c) {
        p->setPen(kTextDim);
        p->drawText(QRectF(0, 0, w, h), Qt::AlignCenter,
                    tr("MIDI-Clip in der Timeline auswählen oder mit „+ MIDI-Clip“ anlegen."));
        return;
    }
    const Project& project = m_controller->project();

    // Zeilen
    for (int pitch = 0; pitch < 128; ++pitch) {
        const double y = yOf(pitch);
        if (y + kRowH < kRollRuler || y > h) continue;
        p->fillRect(QRectF(kKeyWidth, y, w, kRowH), isBlack(pitch) ? QColor(0x17, 0x19, 0x1e) : kRowB);
        if (pitch % 12 == 0) p->fillRect(QRectF(kKeyWidth, y + kRowH - 1, w, 1), kLine);
    }
    // Bereich hinter dem Clip-Ende abdunkeln
    const double endX = xOf(c->in + c->duration);
    if (endX < w) p->fillRect(QRectF(endX, kRollRuler, w - endX, h), QColor(0, 0, 0, 110));

    // Raster: Takt, Schlag, Unterteilung
    const TimeUs beat = project.beatDuration();
    const TimeUs step = gridStep();
    const TimeUs t0 = std::max<TimeUs>(0, timeOf(kKeyWidth) / step * step);
    for (TimeUs t = t0; xOf(t) < w; t += step) {
        const double x = xOf(t);
        const bool isBar = t % (beat * project.beatsPerBar) == 0;
        const bool isBeat = t % beat == 0;
        p->fillRect(QRectF(x, kRollRuler, 1, h), isBar ? QColor(255, 255, 255, 50)
                                                 : isBeat ? QColor(255, 255, 255, 22)
                                                          : QColor(255, 255, 255, 8));
    }

    // Noten
    for (int i = 0; i < static_cast<int>(c->notes.size()); ++i) {
        const Note& n = c->notes[static_cast<std::size_t>(i)];
        const double x0 = xOf(n.start), x1 = xOf(n.start + n.length), y = yOf(n.pitch);
        if (x1 < kKeyWidth || x0 > w || y + kRowH < kRollRuler || y > h) continue;
        QColor fill = kMidiClip;
        fill = fill.lighter(static_cast<int>(70 + n.velocity * 60));
        if (i == m_selectedNote) fill = QColor(0xff, 0xd5, 0x4f);
        const QRectF r(x0, y + 1, std::max(3.0, x1 - x0 - 1), kRowH - 2);
        p->setPen(QPen(fill.darker(160), 1));
        p->setBrush(fill);
        p->drawRoundedRect(r, 2, 2);
        // Anschlagstärke als Balken in der Note
        p->fillRect(QRectF(r.left() + 2, r.bottom() - 3, (r.width() - 4) * n.velocity, 2), QColor(0, 0, 0, 90));
    }

    // Abspielkopf
    const TimeUs content = c->in + (m_controller->positionUs() - c->start);
    if (content >= c->in && content <= c->in + c->duration) {
        const double x = xOf(content);
        if (x >= kKeyWidth) p->fillRect(QRectF(x - 0.75, 0, 1.5, h), kPlayhead);
    }

    // Lineal mit Taktnummern
    p->fillRect(QRectF(0, 0, w, kRollRuler), kRuler);
    p->setPen(kTextDim);
    const TimeUs bar = beat * project.beatsPerBar;
    for (TimeUs t = std::max<TimeUs>(0, timeOf(kKeyWidth) / bar * bar); xOf(t) < w; t += bar) {
        const double x = xOf(t);
        if (x < kKeyWidth - 1) continue;
        p->fillRect(QRectF(x, kRollRuler - 7, 1, 7), kTextDim);
        p->drawText(QPointF(x + 3, 14), QString::number(t / bar + 1));
    }

    // Tastatur
    p->fillRect(QRectF(0, kRollRuler, kKeyWidth, h), QColor(0xe8, 0xea, 0xee));
    for (int pitch = 0; pitch < 128; ++pitch) {
        const double y = yOf(pitch);
        if (y + kRowH < kRollRuler || y > h) continue;
        if (isBlack(pitch)) {
            p->fillRect(QRectF(0, y, kKeyWidth * 0.62, kRowH), QColor(0x22, 0x24, 0x2a));
        } else {
            p->fillRect(QRectF(0, y + kRowH - 0.5, kKeyWidth, 0.5), QColor(0xb0, 0xb4, 0xbc));
        }
        if (pitch % 12 == 0) {
            p->setPen(QColor(0x50, 0x55, 0x60));
            p->drawText(QPointF(kKeyWidth - 26, y + kRowH - 3), noteName(pitch));
        }
    }
    p->fillRect(QRectF(0, 0, kKeyWidth, kRollRuler), kRuler);
    p->fillRect(QRectF(kKeyWidth - 1, kRollRuler, 1, h), kLine);
}

// -------------------------------------------------------------- ScopeItem

ScopeItem::ScopeItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFillColor(QColor(0x0c, 0x0d, 0x10));
}

void ScopeItem::setController(AppController* controller) {
    if (controller == m_controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller)
        connect(m_controller, &AppController::frameReady, this, [this](const QImage& img) {
            if (!isVisible()) return;
            m_frame = img;
            analyse(m_frame);
            update();
        });
    emit controllerChanged();
}

void ScopeItem::setMode(int mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    analyse(m_frame);
    update();
    emit modeChanged();
}

void ScopeItem::analyse(const QImage& frame) {
    if (frame.isNull()) return;
    // Auf höchstens 320 Spalten verkleinern: genug für die Anzeige.
    const QImage src = frame.width() > 320
                           ? frame.scaledToWidth(320, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888)
                           : frame.convertToFormat(QImage::Format_RGBA8888);
    const int sw = src.width(), sh = src.height();
    constexpr int H = 200;

    if (m_mode == 2) {
        // Histogramm R, G, B und Helligkeit
        std::vector<int> hist(256 * 4, 0);
        for (int y = 0; y < sh; ++y) {
            const uchar* p = src.constScanLine(y);
            for (int x = 0; x < sw; ++x, p += 4) {
                hist[p[0]]++;
                hist[256 + p[1]]++;
                hist[512 + p[2]]++;
                hist[768 + static_cast<int>(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2])]++;
            }
        }
        int maxv = 1;
        for (int v : hist) maxv = std::max(maxv, v);
        m_scope = QImage(256, H, QImage::Format_ARGB32_Premultiplied);
        m_scope.fill(Qt::transparent);
        QPainter p(&m_scope);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        const QColor colors[4] = {QColor(220, 60, 60, 160), QColor(60, 200, 80, 160),
                                  QColor(70, 110, 240, 160), QColor(200, 200, 200, 90)};
        for (int c = 0; c < 4; ++c) {
            QPainterPath path;
            path.moveTo(0, H);
            for (int i = 0; i < 256; ++i)
                path.lineTo(i, H - std::sqrt(static_cast<double>(hist[c * 256 + i]) / maxv) * (H - 4));
            path.lineTo(255, H);
            path.closeSubpath();
            p.fillPath(path, colors[c]);
        }
        return;
    }

    // Waveform: Spalten = Bildspalten, Zeilen = Helligkeit
    const bool parade = m_mode == 1;
    const int cols = parade ? sw * 3 : sw;
    std::vector<int> acc(static_cast<std::size_t>(cols) * H, 0);
    for (int y = 0; y < sh; ++y) {
        const uchar* p = src.constScanLine(y);
        for (int x = 0; x < sw; ++x, p += 4) {
            if (parade) {
                for (int c = 0; c < 3; ++c) {
                    const int row = (H - 1) - p[c] * (H - 1) / 255;
                    acc[static_cast<std::size_t>(row) * cols + c * sw + x]++;
                }
            } else {
                const int luma = static_cast<int>(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
                acc[static_cast<std::size_t>((H - 1) - luma * (H - 1) / 255) * cols + x]++;
            }
        }
    }
    m_scope = QImage(cols, H, QImage::Format_ARGB32);
    const double gain = 255.0 / std::max(1.0, sh * 0.06);
    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_scope.scanLine(y));
        for (int x = 0; x < cols; ++x) {
            const int v = std::min(255, static_cast<int>(acc[static_cast<std::size_t>(y) * cols + x] * gain));
            if (parade) {
                const int c = x / sw;
                line[x] = qRgba(c == 0 ? v : v / 5, c == 1 ? v : v / 5, c == 2 ? v : v / 4, 255);
            } else {
                line[x] = qRgba(v / 3, v, v / 2, 255);
            }
        }
    }
}

void ScopeItem::paint(QPainter* p) {
    const QRectF area(36, 8, width() - 44, height() - 16);
    // Hilfslinien bei 0, 25, 50, 75, 100 %
    QFont f = p->font();
    f.setPixelSize(9);
    p->setFont(f);
    for (int i = 0; i <= 4; ++i) {
        const double y = area.bottom() - area.height() * i / 4.0;
        p->fillRect(QRectF(area.left(), y, area.width(), 1), QColor(255, 255, 255, 25));
        p->setPen(kTextDim);
        p->drawText(QPointF(4, y + 3), m_mode == 2 ? QString() : QString::number(i * 25));
    }
    if (!m_scope.isNull()) {
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(area, m_scope);
    }
}
