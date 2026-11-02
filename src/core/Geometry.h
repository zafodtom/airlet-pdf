#pragma once
#include <QPointF>
#include <QSizeF>
namespace airlet {
int normalizedRotation(int rotation);
QSizeF rotatedSize(QSizeF size, int rotation);
QPointF rotatePoint(QPointF point, QSizeF size, int rotation);
QPointF unrotatePoint(QPointF point, QSizeF size, int rotation);
} // namespace airlet
