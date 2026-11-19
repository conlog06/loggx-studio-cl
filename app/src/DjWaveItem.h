// Wellenform eines DJ-Decks: Übersicht (ganze Spur) oder mitlaufend
// gezoomt mit Beat-Raster – Farben wie üblich: Bass blau, Mitten orange,
// Höhen weiß.
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

class DjController;

class DjWaveItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(DjController* controller READ controller WRITE setController NOTIFY changed)
    Q_PROPERTY(int deck READ deck WRITE setDeck NOTIFY changed)
    Q_PROPERTY(bool overview READ overview WRITE setOverview NOTIFY changed)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY changed)   // sichtbare Sekunden (gezoomt)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY changed)

public:
    explicit DjWaveItem(QQuickItem* parent = nullptr);
    DjController* controller() const { return m_controller; }
    void setController(DjController* c);
    int deck() const { return m_deck; }
    void setDeck(int d) { m_deck = d; emit changed(); update(); }
    bool overview() const { return m_overview; }
    void setOverview(bool o) { m_overview = o; emit changed(); update(); }
    double zoom() const { return m_zoom; }
    void setZoom(double z) { m_zoom = z; emit changed(); update(); }
    QColor accent() const { return m_accent; }
    void setAccent(const QColor& c) { m_accent = c; emit changed(); update(); }
    void paint(QPainter* p) override;

signals:
    void changed();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QPointer<DjController> m_controller;
    int m_deck = 0;
    bool m_overview = false;
    double m_zoom = 8.0;
    QColor m_accent{0x5b, 0x8c, 0xff};
    double m_dragX = 0, m_dragPos = 0;
    bool m_dragging = false;
    // Leistung: nur neu zeichnen, wenn sich etwas sichtbar ändert; Wellenform
    // direkt in ein Bild schreiben, Übersicht einmal zwischenspeichern
    void onTick();
    std::uint64_t signature() const;
    std::uint64_t m_lastSig = 0;
    QImage m_frame;                 // gezoomte Ansicht, wird wiederverwendet
    QImage m_overviewImg, m_overviewDark;
    const void* m_overviewTrack = nullptr;
    int m_overviewStyle = -1;
    void renderWave(QImage& img, double t0, double t1, double darkUntil) const;
};
