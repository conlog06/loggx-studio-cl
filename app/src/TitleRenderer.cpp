#include "TitleRenderer.h"

#include <algorithm>
#include <cstring>

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

loggx::TitleRasterizer qtTitleRasterizer() {
    return [](const loggx::Title& t, int w, int h, std::vector<std::uint8_t>& rgba) {
        QImage image(w, h, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        QFont font;
        if (!t.font.empty()) font.setFamily(QString::fromStdString(t.font));
        font.setPixelSize(std::max(4, static_cast<int>(t.size * h)));
        font.setBold(t.bold);
        p.setFont(font);
        const QFontMetricsF fm(font);

        const QStringList lines = QString::fromStdString(t.text).split(QLatin1Char('\n'));
        const double lineH = fm.lineSpacing();
        const double blockH = lineH * lines.size();
        const double top = t.posY * h - blockH / 2.0;
        const double margin = w * 0.05;

        // Zeilen-Rechtecke bestimmen (für Balken und Ausrichtung)
        QList<QRectF> rects;
        for (int i = 0; i < lines.size(); ++i) {
            const double lw = fm.horizontalAdvance(lines[i]);
            double x = (w - lw) / 2.0;
            if (t.align == 0) x = margin;
            else if (t.align == 2) x = w - margin - lw;
            rects.append(QRectF(x, top + i * lineH, lw, lineH));
        }

        if (t.box > 0.0) {
            QRectF all;
            for (const QRectF& r : rects) all = all.isNull() ? r : all.united(r);
            const double pad = lineH * 0.35;
            QColor bg(0, 0, 0);
            bg.setAlphaF(std::clamp(t.box, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(all.adjusted(-pad, -pad * 0.5, pad, pad * 0.5), pad * 0.4, pad * 0.4);
        }

        const QColor color = QColor::fromRgba(t.color);
        for (int i = 0; i < lines.size(); ++i) {
            const QPointF base(rects[i].left(), rects[i].top() + fm.ascent());
            if (t.shadow) {
                QColor shadow(0, 0, 0, static_cast<int>(150 * color.alphaF()));
                p.setPen(shadow);
                p.drawText(base + QPointF(h * 0.004, h * 0.004), lines[i]);
            }
            p.setPen(color);
            p.drawText(base, lines[i]);
        }
        p.end();

        // Vormultipliziert -> gerade Farben, wie die Engine sie erwartet.
        const QImage straight = image.convertToFormat(QImage::Format_RGBA8888);
        rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgba.data() + static_cast<std::size_t>(y) * w * 4, straight.constScanLine(y),
                        static_cast<std::size_t>(w) * 4);
    };
}
