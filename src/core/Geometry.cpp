#include "Geometry.h"
namespace airlet {
int normalizedRotation(int r) {
    return (r % 4 + 4) % 4;
}
QSizeF rotatedSize(QSizeF s, int r) {
    return normalizedRotation(r) % 2 ? QSizeF(s.height(), s.width()) : s;
}
QPointF rotatePoint(QPointF p, QSizeF s, int r) {
    switch (normalizedRotation(r)) {
    case 1:
        return {s.height() - p.y(), p.x()};
    case 2:
        return {s.width() - p.x(), s.height() - p.y()};
    case 3:
        return {p.y(), s.width() - p.x()};
    default:
        return p;
    }
}
QPointF unrotatePoint(QPointF p, QSizeF s, int r) {
    return rotatePoint(p, rotatedSize(s, r), -r);
}
} // namespace airlet
