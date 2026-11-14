// Selbst gezeichnete Elemente der Oberfläche: Vorschau und Timeline.
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

#include "AppController.h"

class ViewerItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit ViewerItem(QQuickItem* parent = nullptr);
    AppController* controller() const { return m_controller; }
    void setController(AppController* controller);
    void paint(QPainter* painter) override;

signals:
    void controllerChanged();

private:
    QPointer<AppController> m_controller;
    QImage m_image;
};

class TimelineItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond WRITE setPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double scrollX READ scrollX WRITE setScrollX NOTIFY viewChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY viewChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth NOTIFY viewChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY viewChanged)
public:
    explicit TimelineItem(QQuickItem* parent = nullptr);

    AppController* controller() const { return m_controller; }
    void setController(AppController* controller);
    double pixelsPerSecond() const { return m_pps; }
    void setPixelsPerSecond(double pps);
    double scrollX() const { return m_scrollX; }
    void setScrollX(double x);
    double contentWidth() const;
    double viewportWidth() const;
    double contentHeight() const;

    // Für Drag & Drop aus der Medienliste.
    Q_INVOKABLE double timeAt(double x) const;
    Q_INVOKABLE int trackIndexAt(double y) const;
    Q_INVOKABLE void zoomToFit();

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void viewChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    enum class Drag { None, Scrub, Move, TrimStart, TrimEnd };
    struct Hit {
        QString clipId;
        int trackIndex = -1;
        Drag zone = Drag::None;
    };

    double xOf(loggx::TimeUs t) const;
    loggx::TimeUs timeOf(double x) const;
    Hit hitTest(const QPointF& pos) const;
    loggx::TimeUs snap(loggx::TimeUs t, const QString& ignoreClip, bool* snapped = nullptr) const;
    void followPlayhead();

    QPointer<AppController> m_controller;
    double m_pps = 80.0;
    double m_scrollX = 0.0;

    Drag m_drag = Drag::None;
    QString m_dragClip;
    loggx::TimeUs m_grabOffset = 0;
};
