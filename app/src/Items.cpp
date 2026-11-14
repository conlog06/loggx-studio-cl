#include "Items.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>

using namespace loggx;

namespace {

constexpr double kHeaderWidth = 116.0;
constexpr double kRulerHeight = 26.0;
constexpr double kTrackHeight = 54.0;
constexpr double kEdgeZone = 7.0;
constexpr double kSnapPixels = 8.0;

const QColor kBackground(0x16, 0x18, 0x1d);
const QColor kRuler(0x1f, 0x22, 0x29);
const QColor kRowA(0x1b, 0x1e, 0x24);
const QColor kRowB(0x1e, 0x21, 0x28);
const QColor kLine(0x2d, 0x31, 0x3b);
const QColor kText(0xd7, 0xdb, 0xe4);
const QColor kTextDim(0x8a, 0x91, 0xa0);
const QColor kVideoClip(0x3d, 0x6f, 0xd6);
const QColor kAudioClip(0x2c, 0x9a, 0x6c);
const QColor kPlayhead(0xff, 0x4d, 0x5e);

QString toQ(const std::string& s) { return QString::fromStdString(s); }

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
    }
    emit controllerChanged();
}

void ViewerItem::paint(QPainter* painter) {
    if (m_image.isNull() || width() < 1 || height() < 1) return;
    const double scale = std::min(width() / m_image.width(), height() / m_image.height());
    const double w = m_image.width() * scale, h = m_image.height() * scale;
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(QRectF((width() - w) / 2, (height() - h) / 2, w, h), m_image);
}

// ---------------------------------------------------------- TimelineItem

TimelineItem::TimelineItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFillColor(kBackground);
    setAntialiasing(true);
}

void TimelineItem::setController(AppController* controller) {
    if (controller == m_controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &AppController::projectChanged, this, [this] {
            emit viewChanged();
            update();
        });
        connect(m_controller, &AppController::selectionChanged, this, [this] { update(); });
        connect(m_controller, &AppController::positionChanged, this, [this] {
            followPlayhead();
            update();
        });
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

double TimelineItem::viewportWidth() const { return std::max(1.0, width() - kHeaderWidth); }

double TimelineItem::contentWidth() const {
    const double seconds = m_controller ? m_controller->duration() : 0.0;
    // Hinter dem letzten Clip bleibt Platz zum Anfügen.
    return std::max(viewportWidth(), seconds * m_pps + viewportWidth() * 0.5);
}

double TimelineItem::contentHeight() const {
    const std::size_t tracks = m_controller ? m_controller->project().tracks.size() : 0;
    return kRulerHeight + static_cast<double>(tracks) * kTrackHeight;
}

double TimelineItem::xOf(TimeUs t) const { return kHeaderWidth + t / 1e6 * m_pps - m_scrollX; }

TimeUs TimelineItem::timeOf(double x) const {
    return static_cast<TimeUs>(std::max(0.0, (x - kHeaderWidth + m_scrollX) / m_pps) * 1e6);
}

double TimelineItem::timeAt(double x) const { return timeOf(x) / 1e6; }

int TimelineItem::trackIndexAt(double y) const {
    if (!m_controller || y < kRulerHeight) return -1;
    const int index = static_cast<int>((y - kRulerHeight) / kTrackHeight);
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
    } else {
        const double step = delta.x() != 0 ? delta.x() : delta.y();
        setScrollX(m_scrollX - step);
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

void TimelineItem::paint(QPainter* p) {
    if (!m_controller) return;
    const Project& project = m_controller->project();
    const double w = width(), h = height();
    QFont font = p->font();
    font.setPixelSize(11);
    p->setFont(font);

    // Spuren
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const double y = kRulerHeight + static_cast<double>(i) * kTrackHeight;
        p->fillRect(QRectF(0, y, w, kTrackHeight), i % 2 ? kRowB : kRowA);
        p->fillRect(QRectF(0, y + kTrackHeight - 1, w, 1), kLine);
    }

    // Lineal
    p->fillRect(QRectF(0, 0, w, kRulerHeight), kRuler);
    static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * m_pps >= 72.0) {
            step = s;
            break;
        }
    p->setClipRect(QRectF(kHeaderWidth, 0, w - kHeaderWidth, h));
    const double first = std::floor(m_scrollX / m_pps / step) * step;
    for (double s = first; s * m_pps - m_scrollX < w; s += step) {
        const double x = kHeaderWidth + s * m_pps - m_scrollX;
        p->fillRect(QRectF(x, kRulerHeight - 8, 1, 8), kTextDim);
        p->fillRect(QRectF(x, kRulerHeight, 1, h - kRulerHeight), QColor(255, 255, 255, 10));
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

    // Clips
    const QString selected = m_controller->selectedClipId();
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& track = project.tracks[i];
        const double y = kRulerHeight + static_cast<double>(i) * kTrackHeight + 4;
        for (const Clip& c : track.clips) {
            const double x0 = xOf(c.start), x1 = xOf(c.end());
            if (x1 < kHeaderWidth || x0 > w) continue;
            const QRectF rect(x0, y, std::max(2.0, x1 - x0 - 1), kTrackHeight - 9);
            const bool isSelected = toQ(c.id) == selected;
            QColor fill = track.kind == TrackKind::Video ? kVideoClip : kAudioClip;
            if (track.muted) fill = fill.darker(220);
            if (isSelected) fill = fill.lighter(125);
            QPainterPath path;
            path.addRoundedRect(rect, 5, 5);
            p->fillPath(path, fill);
            p->setPen(QPen(isSelected ? QColor(Qt::white) : fill.darker(140), isSelected ? 2 : 1));
            p->drawPath(path);

            if (rect.width() > 30) {
                const MediaAsset* asset = project.findAsset(c.assetId);
                QString label = asset ? toQ(asset->name) : QStringLiteral("?");
                QStringList badges;
                if (std::abs(c.speed - 1.0) > 1e-6)
                    badges << QStringLiteral("%1×").arg(QString::number(c.speed, 'g', 3));
                if (std::abs(c.pitch) > 1e-6)
                    badges << QStringLiteral("%1%2 HT")
                                  .arg(c.pitch > 0 ? QStringLiteral("+") : QString())
                                  .arg(QString::number(c.pitch, 'g', 3));
                if (c.mask.shape != MaskShape::None) badges << tr("Maske");
                p->save();
                p->setClipRect(rect.adjusted(6, 0, -6, 0).intersected(
                    QRectF(kHeaderWidth, 0, w - kHeaderWidth, h)));
                p->setPen(Qt::white);
                p->drawText(QPointF(std::max(rect.left(), kHeaderWidth) + 8, rect.top() + 17), label);
                if (!badges.isEmpty()) {
                    p->setPen(QColor(255, 255, 255, 190));
                    p->drawText(QPointF(std::max(rect.left(), kHeaderWidth) + 8, rect.top() + 34),
                                badges.join(QStringLiteral(" · ")));
                }
                p->restore();
            }
        }
    }

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
    p->setClipping(false);

    // Spurköpfe (über den Clips, damit nichts hineinragt)
    p->fillRect(QRectF(0, 0, kHeaderWidth, h), kRuler);
    p->fillRect(QRectF(kHeaderWidth - 1, 0, 1, h), kLine);
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& track = project.tracks[i];
        const double y = kRulerHeight + static_cast<double>(i) * kTrackHeight;
        p->fillRect(QRectF(0, y + kTrackHeight - 1, kHeaderWidth, 1), kLine);
        p->fillRect(QRectF(0, y, 3, kTrackHeight - 1),
                    track.kind == TrackKind::Video ? kVideoClip : kAudioClip);
        p->setPen(track.muted ? kTextDim : kText);
        p->drawText(QPointF(12, y + 23), toQ(track.name));
        p->setPen(track.muted ? kPlayhead : kTextDim);
        const QString state = track.kind == TrackKind::Video
                                  ? (track.muted ? tr("ausgeblendet") : tr("sichtbar"))
                                  : (track.muted ? tr("stumm") : tr("an"));
        p->drawText(QPointF(12, y + 39), state);
    }
}
