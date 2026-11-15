// Selbst gezeichnete Elemente der Oberfläche: Vorschau, Timeline,
// Piano-Roll und Scopes.
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

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void reportSize();
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
    Q_PROPERTY(double scrollY READ scrollY WRITE setScrollY NOTIFY viewChanged)
public:
    explicit TimelineItem(QQuickItem* parent = nullptr);

    AppController* controller() const { return m_controller; }
    void setController(AppController* controller);
    double pixelsPerSecond() const { return m_pps; }
    void setPixelsPerSecond(double pps);
    double scrollX() const { return m_scrollX; }
    void setScrollX(double x);
    double scrollY() const { return m_scrollY; }
    void setScrollY(double y);
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
    // Rechtsklick: auf Spurkopf (header), Clip (clipId) oder leere Fläche
    void contextMenuRequested(double x, double y, int trackIndex, bool header, const QString& clipId, double seconds);

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
    double yOfTrack(int index) const;
    loggx::TimeUs timeOf(double x) const;
    Hit hitTest(const QPointF& pos) const;
    loggx::TimeUs snap(loggx::TimeUs t, const QString& ignoreClip, bool* snapped = nullptr) const;
    void followPlayhead();
    void paintClipContent(QPainter* p, const loggx::Track& track, const loggx::Clip& clip,
                          const QRectF& rect, const QColor& base);

    QPointer<AppController> m_controller;
    double m_pps = 80.0;
    double m_scrollX = 0.0;
    double m_scrollY = 0.0;

    Drag m_drag = Drag::None;
    QString m_dragClip;
    loggx::TimeUs m_grabOffset = 0;
};

// Noteneditor für den ausgewählten MIDI-Clip.
class PianoRollItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(int gridDivision READ gridDivision WRITE setGridDivision NOTIFY viewChanged)
    Q_PROPERTY(bool hasClip READ hasClip NOTIFY viewChanged)
    Q_PROPERTY(int selectedNote READ selectedNote NOTIFY noteSelectionChanged)
    Q_PROPERTY(double selectedVelocity READ selectedVelocity NOTIFY noteSelectionChanged)
public:
    explicit PianoRollItem(QQuickItem* parent = nullptr);
    AppController* controller() const { return m_controller; }
    void setController(AppController* controller);
    int gridDivision() const { return m_division; }
    void setGridDivision(int d);
    bool hasClip() const { return clip() != nullptr; }
    int selectedNote() const { return m_selectedNote; }
    double selectedVelocity() const;
    Q_INVOKABLE void setSelectedVelocity(double v);
    Q_INVOKABLE void deleteSelectedNote();
    Q_INVOKABLE void zoomToFit();

    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void viewChanged();
    void noteSelectionChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    enum class Drag { None, Move, Resize };
    const loggx::Clip* clip() const;
    double xOf(loggx::TimeUs contentTime) const;
    loggx::TimeUs timeOf(double x) const;
    double yOf(int pitch) const;
    int pitchAt(double y) const;
    loggx::TimeUs gridStep() const;
    int noteAt(const QPointF& pos, bool* onEdge) const;
    void ensureView();

    QPointer<AppController> m_controller;
    QString m_viewClip;          // Clip, auf den die Ansicht eingestellt ist
    int m_division = 4;          // Raster: Teile pro Schlag (4 = Sechzehntel)
    double m_pps = 100.0;
    double m_scrollX = 0.0;
    double m_scrollY = 0.0;
    int m_selectedNote = -1;
    Drag m_drag = Drag::None;
    loggx::TimeUs m_grabTime = 0;
    int m_grabPitch = 0;
    loggx::Note m_grabNote;
    loggx::TimeUs m_lastLength = 0;
};

// Waveform-Monitor und Histogramm des aktuellen Vorschaubilds.
class ScopeItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
public:
    explicit ScopeItem(QQuickItem* parent = nullptr);
    AppController* controller() const { return m_controller; }
    void setController(AppController* controller);
    int mode() const { return m_mode; }
    void setMode(int mode);
    void paint(QPainter* painter) override;

signals:
    void controllerChanged();
    void modeChanged();

private:
    void analyse(const QImage& frame);
    QPointer<AppController> m_controller;
    int m_mode = 0;   // 0 Waveform (Luma), 1 RGB-Parade, 2 Histogramm
    QImage m_frame;
    QImage m_scope;
};
