// Vektor-Icons, mit QPainter gezeichnet (keine Bilddateien, keine
// SVG-Abhängigkeit, auf jedem System gleich).
#pragma once

#include <QColor>
#include <QQuickPaintedItem>

class IconItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
public:
    explicit IconItem(QQuickItem* parent = nullptr);
    QString name() const { return m_name; }
    void setName(const QString& name);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    void paint(QPainter* painter) override;

    static QStringList names();

signals:
    void changed();

private:
    QString m_name;
    QColor m_color = QColor(0xe8, 0xeb, 0xf1);
};
