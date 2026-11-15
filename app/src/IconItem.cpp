#include "IconItem.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>

IconItem::IconItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setImplicitSize(18, 18);
}

void IconItem::setName(const QString& name) {
    if (name == m_name) return;
    m_name = name;
    update();
    emit changed();
}

void IconItem::setColor(const QColor& color) {
    if (color == m_color) return;
    m_color = color;
    update();
    emit changed();
}

QStringList IconItem::names() {
    return {"play", "pause", "stop", "record", "skip-back", "skip-forward", "step-back",
            "step-forward", "scissors", "trash", "plus", "minus", "film", "music", "folder",
            "save", "undo", "redo", "text", "export", "import", "sliders", "palette", "piano",
            "magnet", "keyboard", "settings", "shield", "close", "check", "image", "wave",
            "layers", "transition", "metronome", "key", "chevron-down", "chevron-right",
            "fit", "home", "clock", "sparkles", "file", "beat-cut", "mic", "copy"};
}

namespace {

// Alle Icons in einem 24×24-Raster, Linienstärke 2 (wie gängige Icon-Sets).
void build(const QString& n, QPainterPath& stroke, QPainterPath& fill) {
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        stroke.moveTo(x1, y1);
        stroke.lineTo(x2, y2);
    };
    auto poly = [](QPainterPath& p, std::initializer_list<QPointF> pts, bool close) {
        auto it = pts.begin();
        p.moveTo(*it);
        for (++it; it != pts.end(); ++it) p.lineTo(*it);
        if (close) p.closeSubpath();
    };

    if (n == "play") {
        poly(fill, {{7, 4.5}, {19.5, 12}, {7, 19.5}}, true);
    } else if (n == "pause") {
        fill.addRoundedRect(6, 4.5, 4, 15, 1, 1);
        fill.addRoundedRect(14, 4.5, 4, 15, 1, 1);
    } else if (n == "stop") {
        fill.addRoundedRect(6, 6, 12, 12, 2, 2);
    } else if (n == "record") {
        fill.addEllipse(QPointF(12, 12), 7, 7);
    } else if (n == "skip-back") {
        fill.addRoundedRect(5, 5, 2.5, 14, 1, 1);
        poly(fill, {{19, 5}, {9, 12}, {19, 19}}, true);
    } else if (n == "skip-forward") {
        fill.addRoundedRect(16.5, 5, 2.5, 14, 1, 1);
        poly(fill, {{5, 5}, {15, 12}, {5, 19}}, true);
    } else if (n == "step-back" || n == "chevron-left") {
        poly(stroke, {{15, 6}, {9, 12}, {15, 18}}, false);
    } else if (n == "step-forward" || n == "chevron-right") {
        poly(stroke, {{9, 6}, {15, 12}, {9, 18}}, false);
    } else if (n == "chevron-down") {
        poly(stroke, {{6, 9}, {12, 15}, {18, 9}}, false);
    } else if (n == "scissors") {
        stroke.addEllipse(QPointF(6, 6), 3, 3);
        stroke.addEllipse(QPointF(6, 18), 3, 3);
        line(20, 4, 8.2, 15.8);
        line(14.5, 14.5, 20, 20);
        line(8.2, 8.2, 12, 12);
    } else if (n == "trash") {
        line(3, 6, 21, 6);
        poly(stroke, {{19, 6}, {18, 20}, {6, 20}, {5, 6}}, false);
        poly(stroke, {{9, 6}, {9, 3.5}, {15, 3.5}, {15, 6}}, false);
        line(10, 10.5, 10, 16.5);
        line(14, 10.5, 14, 16.5);
    } else if (n == "plus") {
        line(12, 5, 12, 19);
        line(5, 12, 19, 12);
    } else if (n == "minus") {
        line(5, 12, 19, 12);
    } else if (n == "film") {
        stroke.addRoundedRect(3, 3, 18, 18, 2.5, 2.5);
        line(7.5, 3, 7.5, 21);
        line(16.5, 3, 16.5, 21);
        line(3, 12, 21, 12);
        line(3, 7.5, 7.5, 7.5);
        line(3, 16.5, 7.5, 16.5);
        line(16.5, 7.5, 21, 7.5);
        line(16.5, 16.5, 21, 16.5);
    } else if (n == "music") {
        poly(stroke, {{9, 18}, {9, 5}, {21, 3}, {21, 16}}, false);
        stroke.addEllipse(QPointF(6, 18), 3, 3);
        stroke.addEllipse(QPointF(18, 16), 3, 3);
    } else if (n == "folder") {
        poly(stroke, {{3, 6.5}, {3, 19}, {21, 19}, {21, 8.5}, {12.5, 8.5}, {10.5, 5.5}, {4, 5.5}}, true);
    } else if (n == "save") {
        poly(stroke, {{5, 3.5}, {16.5, 3.5}, {20.5, 7.5}, {20.5, 20.5}, {3.5, 20.5}, {3.5, 3.5}}, true);
        stroke.addRect(7.5, 13, 9, 7.5);
        poly(stroke, {{7.5, 3.5}, {7.5, 8}, {14.5, 8}}, false);
    } else if (n == "undo") {
        poly(stroke, {{8.5, 14}, {3.5, 9}, {8.5, 4}}, false);
        stroke.moveTo(3.5, 9);
        stroke.lineTo(15, 9);
        stroke.arcTo(10, 9, 10, 10, 90, -180);
        stroke.lineTo(11, 19);
    } else if (n == "redo") {
        poly(stroke, {{15.5, 14}, {20.5, 9}, {15.5, 4}}, false);
        stroke.moveTo(20.5, 9);
        stroke.lineTo(9, 9);
        stroke.arcTo(4, 9, 10, 10, 90, 180);
        stroke.lineTo(13, 19);
    } else if (n == "text") {
        poly(stroke, {{5, 7.5}, {5, 5}, {19, 5}, {19, 7.5}}, false);
        line(12, 5, 12, 19);
        line(9, 19, 15, 19);
    } else if (n == "export") {
        line(12, 15, 12, 3);
        poly(stroke, {{7, 8}, {12, 3}, {17, 8}}, false);
        poly(stroke, {{4, 14}, {4, 20}, {20, 20}, {20, 14}}, false);
    } else if (n == "import") {
        line(12, 3, 12, 15);
        poly(stroke, {{7, 10}, {12, 15}, {17, 10}}, false);
        poly(stroke, {{4, 14}, {4, 20}, {20, 20}, {20, 14}}, false);
    } else if (n == "sliders") {
        line(6, 3, 6, 21);
        line(12, 3, 12, 21);
        line(18, 3, 18, 21);
        fill.addRoundedRect(3.5, 13, 5, 3.5, 1, 1);
        fill.addRoundedRect(9.5, 6.5, 5, 3.5, 1, 1);
        fill.addRoundedRect(15.5, 15, 5, 3.5, 1, 1);
    } else if (n == "palette") {
        stroke.moveTo(12, 3);
        stroke.arcTo(3, 3, 18, 18, 90, 300);
        stroke.cubicTo(18, 17, 14, 15, 15, 18.5);
        stroke.cubicTo(16, 21, 12, 21, 12, 21);
        fill.addEllipse(QPointF(8, 10), 1.5, 1.5);
        fill.addEllipse(QPointF(12, 7), 1.5, 1.5);
        fill.addEllipse(QPointF(16, 10), 1.5, 1.5);
        fill.addEllipse(QPointF(8, 15), 1.5, 1.5);
    } else if (n == "piano") {
        stroke.addRoundedRect(3, 4, 18, 16, 2, 2);
        line(9, 13, 9, 20);
        line(15, 13, 15, 20);
        fill.addRect(7.5, 4, 3, 9);
        fill.addRect(13.5, 4, 3, 9);
    } else if (n == "magnet") {
        stroke.moveTo(5, 3.5);
        stroke.lineTo(5, 12);
        stroke.arcTo(5, 5, 14, 14, 180, 180);
        stroke.lineTo(19, 3.5);
        line(5, 8, 9, 8);
        line(15, 8, 19, 8);
        poly(stroke, {{9, 3.5}, {9, 12}}, false);
        poly(stroke, {{15, 3.5}, {15, 12}}, false);
    } else if (n == "keyboard") {
        stroke.addRoundedRect(2.5, 6, 19, 12, 2, 2);
        for (qreal x : {6.5, 10.0, 13.5, 17.0}) fill.addRect(x - 0.9, 9, 1.8, 1.8);
        line(7, 14.5, 17, 14.5);
    } else if (n == "settings") {
        stroke.addEllipse(QPointF(12, 12), 3, 3);
        stroke.addEllipse(QPointF(12, 12), 7.5, 7.5);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * 3.14159265 / 4;
            line(12 + 7.5 * std::cos(a), 12 + 7.5 * std::sin(a), 12 + 10 * std::cos(a), 12 + 10 * std::sin(a));
        }
    } else if (n == "shield") {
        stroke.moveTo(12, 3);
        stroke.lineTo(20, 6);
        stroke.lineTo(20, 12);
        stroke.cubicTo(20, 17, 16.5, 20, 12, 21);
        stroke.cubicTo(7.5, 20, 4, 17, 4, 12);
        stroke.lineTo(4, 6);
        stroke.closeSubpath();
        poly(stroke, {{8.5, 12}, {11, 14.5}, {15.5, 9.5}}, false);
    } else if (n == "close") {
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
    } else if (n == "check") {
        poly(stroke, {{5, 12.5}, {10, 17.5}, {19.5, 7}}, false);
    } else if (n == "mic") {
        stroke.addRoundedRect(9, 3, 6, 11, 3, 3);
        stroke.moveTo(5.5, 11);
        stroke.cubicTo(5.5, 19, 18.5, 19, 18.5, 11);
        line(12, 17.5, 12, 21);
        line(8.5, 21, 15.5, 21);
    } else if (n == "copy") {
        stroke.addRoundedRect(8.5, 8.5, 12, 12, 2, 2);
        poly(stroke, {{15.5, 5.5}, {15.5, 4.5}, {3.5, 4.5}, {3.5, 15.5}, {5.5, 15.5}}, false);
    } else if (n == "image") {
        stroke.addRoundedRect(3, 4, 18, 16, 2, 2);
        stroke.addEllipse(QPointF(9, 9.5), 1.8, 1.8);
        poly(stroke, {{3.5, 18}, {9.5, 12.5}, {13, 15.5}, {16, 13}, {20.5, 17.5}}, false);
    } else if (n == "wave") {
        const qreal hs[] = {4, 9, 14, 7, 11, 5, 9, 3};
        for (int i = 0; i < 8; ++i) {
            const qreal x = 4 + i * 2.3;
            line(x, 12 - hs[i] / 2, x, 12 + hs[i] / 2);
        }
    } else if (n == "layers") {
        poly(stroke, {{12, 3}, {21, 8}, {12, 13}, {3, 8}}, true);
        poly(stroke, {{3, 12.5}, {12, 17.5}, {21, 12.5}}, false);
        poly(stroke, {{3, 16.5}, {12, 21.5}, {21, 16.5}}, false);
    } else if (n == "transition") {
        poly(fill, {{4, 5}, {12, 12}, {4, 19}}, true);
        poly(stroke, {{20, 5}, {12, 12}, {20, 19}}, true);
    } else if (n == "metronome") {
        poly(stroke, {{7, 21}, {9.5, 3}, {14.5, 3}, {17, 21}}, true);
        line(12, 16, 18, 6);
        line(7.5, 17, 16.5, 17);
    } else if (n == "beat-cut") {
        for (qreal x : {5.0, 10.0, 15.0, 20.0}) line(x, 4, x, 7);
        line(3, 12, 21, 12);
        poly(stroke, {{8, 9}, {8, 15}}, false);
        poly(stroke, {{16, 9}, {16, 15}}, false);
        stroke.addEllipse(QPointF(8, 18.5), 2, 2);
        stroke.addEllipse(QPointF(16, 18.5), 2, 2);
    } else if (n == "key") {
        stroke.addEllipse(QPointF(8, 15.5), 4.5, 4.5);
        line(11.2, 12.3, 20, 3.5);
        line(17, 6.5, 19.5, 9);
        line(14.5, 9, 16.5, 11);
    } else if (n == "fit") {
        poly(stroke, {{3, 8}, {3, 3}, {8, 3}}, false);
        poly(stroke, {{16, 3}, {21, 3}, {21, 8}}, false);
        poly(stroke, {{21, 16}, {21, 21}, {16, 21}}, false);
        poly(stroke, {{8, 21}, {3, 21}, {3, 16}}, false);
    } else if (n == "home") {
        poly(stroke, {{3, 11}, {12, 3.5}, {21, 11}}, false);
        poly(stroke, {{5.5, 9}, {5.5, 20.5}, {18.5, 20.5}, {18.5, 9}}, false);
        poly(stroke, {{10, 20.5}, {10, 14.5}, {14, 14.5}, {14, 20.5}}, false);
    } else if (n == "clock") {
        stroke.addEllipse(QPointF(12, 12), 9, 9);
        poly(stroke, {{12, 7}, {12, 12}, {15.5, 14}}, false);
    } else if (n == "sparkles") {
        poly(fill, {{12, 3}, {13.6, 9.4}, {20, 11}, {13.6, 12.6}, {12, 19}, {10.4, 12.6}, {4, 11}, {10.4, 9.4}}, true);
        poly(fill, {{19, 15}, {19.7, 17.3}, {22, 18}, {19.7, 18.7}, {19, 21}, {18.3, 18.7}, {16, 18}, {18.3, 17.3}}, true);
    } else if (n == "file") {
        poly(stroke, {{6, 3}, {14, 3}, {19, 8}, {19, 21}, {6, 21}}, true);
        poly(stroke, {{14, 3}, {14, 8}, {19, 8}}, false);
    }
}

}  // namespace

void IconItem::paint(QPainter* p) {
    QPainterPath stroke, fill;
    build(m_name, stroke, fill);
    if (stroke.isEmpty() && fill.isEmpty()) return;
    const qreal s = std::min(width(), height()) / 24.0;
    p->setRenderHint(QPainter::Antialiasing);
    p->translate((width() - 24 * s) / 2, (height() - 24 * s) / 2);
    p->scale(s, s);
    if (!fill.isEmpty()) p->fillPath(fill, m_color);
    if (!stroke.isEmpty()) {
        QPen pen(m_color, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p->strokePath(stroke, pen);
    }
}
