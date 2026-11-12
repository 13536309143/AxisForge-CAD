#include "Viewport.h"
#include <QPainter>
#include <QVector4D>
#include <algorithm>
#include <cmath>
namespace af {
namespace {
bool clipLine(QVector4D &a, QVector4D &b) {
    const auto start = a, end = b;
    double first = 0, last = 1;
    const double f[] = {double(a.w() + a.x()), double(a.w() - a.x()), double(a.w() + a.y()),
                        double(a.w() - a.y()), double(a.w() + a.z()), double(a.w() - a.z()),
                        double(a.w()) - 1e-6};
    const double g[] = {double(b.w() + b.x()), double(b.w() - b.x()), double(b.w() + b.y()),
                        double(b.w() - b.y()), double(b.w() + b.z()), double(b.w() - b.z()),
                        double(b.w()) - 1e-6};
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(f[i]) || !std::isfinite(g[i]))
            return false;
        if (f[i] < 0 && g[i] < 0)
            return false;
        if (f[i] < 0)
            first = qMax(first, f[i] / (f[i] - g[i]));
        if (g[i] < 0)
            last = qMin(last, f[i] / (f[i] - g[i]));
        if (first > last)
            return false;
    }
    a = start + (end - start) * float(first);
    b = start + (end - start) * float(last);
    return true;
}
bool pointInside(const QVector4D &p) {
    return std::isfinite(p.w()) && p.w() > 1e-6f && std::abs(p.x()) <= p.w() && std::abs(p.y()) <= p.w() &&
           std::abs(p.z()) <= p.w();
}
} // namespace
void Viewport::paintMateGuide(QPainter &p) {
    const auto guide = currentMateGuide();
    if (!guide.valid)
        return;
    p.save();
    const QRectF panel(18, 64, qMin(600, width() - 36), previewNote_.isEmpty() ? 80 : 102);
    p.setPen(QPen(QColor("#38546c"), 1));
    p.setBrush(QColor(12, 23, 35, 235));
    p.drawRoundedRect(panel, 7, 7);
    p.setPen(guide.enabled ? (guide.satisfied ? QColor("#95e7d0") : QColor("#ff9292")) : QColor("#b1b8c5"));
    p.setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
    p.drawText(panel.adjusted(12, 5, -12, -48), Qt::AlignVCenter, guide.title);
    p.setFont(QFont("Microsoft YaHei", 9));
    p.setPen(QColor("#cfdfeb"));
    p.drawText(panel.left() + 12, panel.top() + 44,
               p.fontMetrics().elidedText(guide.detail, Qt::ElideRight, int(panel.width() - 24)));
    p.setPen(QColor("#9cb0c4"));
    p.drawText(panel.left() + 12, panel.top() + 65,
               QStringLiteral("青色 A / 橙色 B · 标记置顶显示，辅助观察配合"));
    if (!previewNote_.isEmpty()) {
        p.setPen(QColor("#f8d681"));
        p.drawText(panel.left() + 12, panel.top() + 88,
                   p.fontMetrics().elidedText(previewNote_, Qt::ElideRight, int(panel.width() - 24)));
    }
    if (!guide.visible) {
        p.restore();
        return;
    }
    const auto matrix = projection() * view();
    auto screen = [&](QVector4D v) {
        return QPointF((v.x() / v.w() + 1) * width() * .5, (1 - v.y() / v.w()) * height() * .5);
    };
    p.save();
    p.setClipRegion(QRegion(rect()).subtracted(QRegion(panel.toAlignedRect())));
    for (int pass = 0; pass < 2; ++pass)
        for (const auto &line : guide.lines) {
            auto a = matrix * QVector4D(line.a, 1), b = matrix * QVector4D(line.b, 1);
            if (!clipLine(a, b))
                continue;
            p.setPen(pass ? QPen(line.color, 2.1, line.dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap)
                          : QPen(QColor(8, 16, 26, 190), 5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(screen(a), screen(b));
        }
    p.restore();
    QVector<QRectF> occupied{panel};
    for (const auto &label : guide.labels) {
        auto point = matrix * QVector4D(label.position, 1);
        if (!pointInside(point))
            continue;
        const auto at = screen(point);
        QRectF box = p.fontMetrics().boundingRect(label.text);
        box.adjust(-7, -4, 7, 4);
        box.moveTopLeft(at + QPointF(8, -box.height() - 5));
        if (box.width() > width() - 24)
            continue;
        box.moveLeft(qBound(12.0, box.left(), width() - 12 - box.width()));
        box.moveTop(qBound(12.0, box.top(), height() - 48 - box.height()));
        for (int attempt = 0; attempt < 8; ++attempt) {
            bool overlap = false;
            for (const auto &r : occupied)
                if (box.intersects(r)) {
                    overlap = true;
                    break;
                }
            if (!overlap)
                break;
            box.translate(0, box.height() + 4);
        }
        if (box.bottom() > height() - 45)
            continue;
        p.setPen(QPen(label.color, 1));
        p.setBrush(QColor(13, 23, 35, 240));
        p.drawRoundedRect(box, 4, 4);
        p.drawText(box.adjusted(7, 3, -7, -3), Qt::AlignCenter, label.text);
        occupied << box;
    }
    p.restore();
}
} // namespace af
