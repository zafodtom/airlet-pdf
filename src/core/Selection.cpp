#include "Selection.h"
#include "Geometry.h"
#include <algorithm>
#include <limits>

namespace airlet {
int nearestGlyph(const TextPage &text, QPointF point) {
    int best = -1;
    double distance = std::numeric_limits<double>::max();
    for (int i = 0; i < text.glyphs.size(); ++i) {
        const auto &r = text.glyphs[i].bounds;
        if (r.isEmpty() || text.glyphs[i].text.trimmed().isEmpty())
            continue;
        const double dx = point.x() - std::clamp(point.x(), r.left(), r.right());
        const double dy = point.y() - std::clamp(point.y(), r.top(), r.bottom());
        const double d = dx * dx + dy * dy;
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
QString selectedText(const TextPage &text, int first, int last) {
    if (!text.copyAllowed || first < 0 || last < 0 || first >= text.glyphs.size() ||
        last >= text.glyphs.size())
        return {};
    if (first > last)
        std::swap(first, last);
    QString result;
    for (int i = first; i <= last; ++i)
        result += text.glyphs[i].text;
    result.replace("\r\n", "\n");
    result.replace('\r', '\n');
    return result;
}
QVector<QRectF> highlightBands(const QVector<QRectF> &bounds) {
    QVector<QRectF> result;
    QRectF band, previous;
    int direction = 0; // 1 horizontal, 2 vertical (intrinsic page rotation)
    auto flush = [&] {
        if (!band.isEmpty())
            result.append(band);
        band = {};
        previous = {};
        direction = 0;
    };
    for (const auto &rect : bounds) {
        if (rect.isEmpty()) {
            flush();
            continue;
        }
        if (band.isEmpty()) {
            band = previous = rect;
            continue;
        }
        const double overlapY =
            std::min(previous.bottom(), rect.bottom()) - std::max(previous.top(), rect.top());
        const double overlapX =
            std::min(previous.right(), rect.right()) - std::max(previous.left(), rect.left());
        const double gapX =
            std::max({0., rect.left() - previous.right(), previous.left() - rect.right()});
        const double gapY =
            std::max({0., rect.top() - previous.bottom(), previous.top() - rect.bottom()});
        const bool horizontal = direction != 2 &&
                                overlapY >= .3 * std::min(previous.height(), rect.height()) &&
                                gapX <= .9 * std::max(previous.height(), rect.height());
        const bool vertical = direction != 1 &&
                              overlapX >= .3 * std::min(previous.width(), rect.width()) &&
                              gapY <= .9 * std::max(previous.width(), rect.width());
        if (horizontal || vertical) {
            direction = horizontal ? 1 : 2;
            band = band.united(rect);
        } else {
            flush();
            band = rect;
        }
        previous = rect;
    }
    flush();
    return result;
}
QRectF rotateRect(QRectF r, QSizeF size, int rotation) {
    const QPointF points[] = {
        rotatePoint(r.topLeft(), size, rotation), rotatePoint(r.topRight(), size, rotation),
        rotatePoint(r.bottomLeft(), size, rotation), rotatePoint(r.bottomRight(), size, rotation)};
    double l = points[0].x(), t = points[0].y(), right = l, b = t;
    for (auto p : points) {
        l = std::min(l, p.x());
        t = std::min(t, p.y());
        right = std::max(right, p.x());
        b = std::max(b, p.y());
    }
    return {QPointF(l, t), QPointF(right, b)};
}
} // namespace airlet
