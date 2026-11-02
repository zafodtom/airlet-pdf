#pragma once
#include "PdfEngine.h"

namespace airlet {
int nearestGlyph(const TextPage &text, QPointF point);
QString selectedText(const TextPage &text, int first, int last);
QRectF rotateRect(QRectF rect, QSizeF pageSize, int rotation);
} // namespace airlet
