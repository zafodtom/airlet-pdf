#include "PdfView.h"
#include "core/Geometry.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace airlet {
namespace {
constexpr int tileEdge = 512;
constexpr double gap = 16.;
} // namespace
PdfView::PdfView(QWidget *parent) : QAbstractScrollArea(parent), service(this) {
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    // A stable viewport width prevents fit-width/vertical-scrollbar oscillation
    // when a landscape sheet is almost exactly as tall as the viewport.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setAutoFillBackground(false);
    connect(&service, &RenderService::opened, this, [this](quint64 s, OpenResult result) {
        if (s != session)
            return;
        if (result.error != OpenError::None) {
            emit openFailed(path, result);
            return;
        }
        if (!result.first.points.isValid()) {
            emit openFailed(
                path,
                {OpenError::Invalid, QStringLiteral("Neplatné rozměry první stránky."), 0, {}});
            return;
        }
        sizes.fill(result.first.points, result.pages);
        rotations.fill(0, result.pages);
        firstPending = true;
        rebuildLayout();
        if (fitMode == 1)
            fitWidth();
        else if (fitMode == 2)
            fitPage();
        else
            schedule();
        goToPage(0);
        emit documentOpened(path, result.pages);
        emit pageChanged(1, result.pages);
    });
    connect(&service, &RenderService::rendered, this, &PdfView::acceptResult);
}
void PdfView::openFile(const QString &p, const QString &password) {
    closeDocument();
    path = p;
    openingTimer.start();
    service.open(session, path, password);
}
void PdfView::closeDocument() {
    ++session;
    service.close(session);
    sizes.clear();
    rotations.clear();
    pages.clear();
    sharp.clear();
    previews.clear();
    firstPending = false;
    rebuildLayout();
    viewport()->update();
    emit pageChanged(0, 0);
}
QRectF PdfView::pageRect(int p) const {
    return pages[p];
}
int PdfView::pageAtY(double y) const {
    auto it = std::lower_bound(pages.cbegin(), pages.cend(), y,
                               [](const QRectF &r, double v) { return r.bottom() < v; });
    return pages.isEmpty() ? -1 : std::min(int(it - pages.cbegin()), int(pages.size() - 1));
}
int PdfView::currentPage() const {
    return pageAtY(verticalScrollBar()->value() + viewport()->height() * .25);
}
void PdfView::rebuildLayout() {
    changingLayout = true;
    const double k = logicalScale();
    documentWidth = viewport()->width();
    for (int i = 0; i < sizes.size(); ++i)
        documentWidth =
            std::max(documentWidth, rotatedSize(sizes[i], rotations[i]).width() * k + 2 * gap);
    pages.resize(sizes.size());
    double y = gap;
    for (int i = 0; i < sizes.size(); ++i) {
        const auto s = rotatedSize(sizes[i], rotations[i]) * k;
        pages[i] = QRectF(QPointF((documentWidth - s.width()) / 2., y), s);
        y += s.height() + gap;
    }
    // QScrollBar uses int. Clamp to avoid integer overflow on pathological PDFs.
    const auto bounded = [](double v) {
        return int(std::clamp(v, 0., double(std::numeric_limits<int>::max() - 1)));
    };
    verticalScrollBar()->setRange(0, bounded(y - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, bounded(documentWidth - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    changingLayout = false;
}
void PdfView::goToPage(int p) {
    if (pages.isEmpty())
        return;
    p = std::clamp(p, 0, int(pages.size() - 1));
    verticalScrollBar()->setValue(qRound(pages[p].top() - gap));
    schedule();
}
void PdfView::setZoom(double value, QPoint anchor) {
    fitMode = 0;
    if (anchor.x() < 0)
        anchor = viewport()->rect().center();
    value = std::clamp(value, .05, 16.);
    const double oldScale = logicalScale();
    const QPointF docPoint =
        anchor + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const int page = pageAtY(docPoint.y());
    QPointF local;
    if (page >= 0)
        local = (docPoint - pages[page].topLeft()) / oldScale;
    scale = value;
    rebuildLayout();
    if (page >= 0) {
        const auto newPoint = pages[page].topLeft() + local * logicalScale();
        changingLayout = true;
        horizontalScrollBar()->setValue(qRound(newPoint.x() - anchor.x()));
        verticalScrollBar()->setValue(qRound(newPoint.y() - anchor.y()));
        changingLayout = false;
    }
    emit zoomChanged(scale);
    schedule();
    viewport()->update();
}
void PdfView::fitWidth() {
    fitMode = 1;
    const int p = currentPage();
    if (p < 0)
        return;
    const auto s = rotatedSize(sizes[p], rotations[p]);
    setZoom((viewport()->width() - 2 * gap) / (s.width() * 96. / 72.));
    fitMode = 1;
}
void PdfView::fitPage() {
    fitMode = 2;
    const int p = currentPage();
    if (p < 0)
        return;
    const auto s = rotatedSize(sizes[p], rotations[p]);
    setZoom(std::min((viewport()->width() - 2 * gap) / (s.width() * 96. / 72.),
                     (viewport()->height() - 2 * gap) / (s.height() * 96. / 72.)));
    fitMode = 2;
    goToPage(p);
}
void PdfView::rotate(int delta) {
    const int p = currentPage();
    if (p < 0)
        return;
    rotations[p] = normalizedRotation(rotations[p] + delta);
    rebuildLayout();
    goToPage(p);
    if (fitMode == 1)
        fitWidth();
    else if (fitMode == 2)
        fitPage();
    viewport()->update();
}
QSize PdfView::pixelSize(int p) const {
    const auto s = pages[p].size() * viewport()->devicePixelRatioF();
    return QSize(qCeil(std::clamp(s.width(), 1., 1000000.)),
                 qCeil(std::clamp(s.height(), 1., 1000000.)));
}
QString PdfView::tileKey(int p, QSize s, QRect t) const {
    return QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
        .arg(p)
        .arg(rotations[p])
        .arg(s.width())
        .arg(s.height())
        .arg(t.x())
        .arg(t.y())
        .arg(session);
}
QString PdfView::previewKey(int p) const {
    return QStringLiteral("p%1/%2/%3").arg(session).arg(p).arg(rotations[p]);
}
void PdfView::schedule() {
    if (changingLayout || pages.isEmpty())
        return;
    const QRectF visible(QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()),
                         viewport()->size());
    const int first = pageAtY(visible.top());
    QVector<RenderRequest> requests;
    QVector<int> visiblePages;
    const double dpr = viewport()->devicePixelRatioF();
    for (int p = first; p < pages.size() && pages[p].top() < visible.bottom(); ++p) {
        if (!pages[p].intersects(visible))
            continue;
        visiblePages.push_back(p);
        const auto key = previewKey(p);
        if (!previews.contains(key)) {
            auto size =
                rotatedSize(sizes[p], rotations[p]).scaled(800, 800, Qt::KeepAspectRatio).toSize();
            size = size.expandedTo(QSize(1, 1));
            requests.push_back({p, rotations[p], size, QRect(QPoint(), size), true, key});
        }
    }
    for (const int p : visiblePages) {
        const auto full = pixelSize(p);
        const QRectF local = visible.intersected(pages[p]).translated(-pages[p].topLeft());
        const QRect pixels = QRectF(local.topLeft() * dpr, local.size() * dpr)
                                 .toAlignedRect()
                                 .intersected(QRect(QPoint(), full));
        for (int y = pixels.top() / tileEdge * tileEdge; y <= pixels.bottom(); y += tileEdge)
            for (int x = pixels.left() / tileEdge * tileEdge; x <= pixels.right(); x += tileEdge) {
                const QRect tile =
                    QRect(x, y, tileEdge, tileEdge).intersected(QRect(QPoint(), full));
                const auto key = tileKey(p, full, tile);
                if (!sharp.contains(key))
                    requests.push_back({p, rotations[p], full, tile, false, key});
            }
    }
    // Only one neighbouring page, preview only, after every visible tile.
    if (!visiblePages.isEmpty()) {
        const int p = visiblePages.back() + 1;
        if (p < pages.size() && !previews.contains(previewKey(p))) {
            auto size = rotatedSize(sizes[p], rotations[p])
                            .scaled(800, 800, Qt::KeepAspectRatio)
                            .toSize()
                            .expandedTo(QSize(1, 1));
            requests.push_back({p, rotations[p], size, QRect(QPoint(), size), true, previewKey(p)});
        }
    }
    generation = service.submit(session, std::move(requests));
    emit pageChanged(currentPage() + 1, pages.size());
}
void PdfView::acceptResult(quint64 s, quint64 g, RenderRequest r, PageInfo info, QImage image,
                           qint64) {
    if (s != session || g != generation || r.page < 0 || r.page >= pages.size())
        return;
    if (info.points.isValid() && info.points != sizes[r.page]) {
        generation = service.submit(session, {});
        const int p = currentPage();
        const double offset = p >= 0 ? verticalScrollBar()->value() - pages[p].top() : 0.;
        sizes[r.page] = info.points;
        rebuildLayout();
        if (p >= 0) {
            changingLayout = true;
            verticalScrollBar()->setValue(qRound(pages[p].top() + offset));
            changingLayout = false;
        }
        // One deferred event coalesces geometry/resize changes, never a timer loop.
        if (!metadataPending) {
            metadataPending = true;
            QTimer::singleShot(0, this, [this] {
                metadataPending = false;
                if (fitMode == 1)
                    fitWidth();
                else if (fitMode == 2)
                    fitPage();
                else
                    schedule();
                viewport()->update();
            });
        }
        return;
    }
    if (image.isNull())
        return;
    if (r.preview)
        previews.put(r.key, image);
    else
        sharp.put(r.key, image);
    viewport()->update(); // paint never submits work
}
void PdfView::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), QColor(66, 69, 73));
    if (pages.isEmpty()) {
        painter.setPen(QColor(225, 225, 225));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         QStringLiteral("Otevřete PDF nebo jej přetáhněte sem"));
        return;
    }
    const QPointF offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const QRectF visible(offset, viewport()->size());
    bool paintedImage = false;
    for (int p = pageAtY(visible.top()); p < pages.size() && pages[p].top() < visible.bottom();
         ++p) {
        const QRectF rect = pages[p].translated(-offset);
        if (!rect.intersects(viewport()->rect()))
            continue;
        painter.fillRect(rect, Qt::white);
        if (const auto *image = previews.get(previewKey(p))) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(rect, *image);
            paintedImage = true;
        }
        const auto full = pixelSize(p);
        const double kx = rect.width() / full.width(), ky = rect.height() / full.height();
        const QRectF local = visible.intersected(pages[p]).translated(-pages[p].topLeft());
        const int x0 = std::max(0, int(local.left() / kx) / tileEdge * tileEdge),
                  y0 = std::max(0, int(local.top() / ky) / tileEdge * tileEdge);
        for (int y = y0; y < full.height() && y * ky <= local.bottom(); y += tileEdge)
            for (int x = x0; x < full.width() && x * kx <= local.right(); x += tileEdge) {
                const QRect tile =
                    QRect(x, y, tileEdge, tileEdge).intersected(QRect(QPoint(), full));
                if (const auto *image = sharp.get(tileKey(p, full, tile))) {
                    painter.drawImage(QRectF(rect.left() + x * kx, rect.top() + y * ky,
                                             tile.width() * kx, tile.height() * ky),
                                      *image);
                    paintedImage = true;
                }
            }
    }
    if (firstPending && paintedImage) {
        firstPending = false;
        emit firstImage(openingTimer.elapsed());
    }
}
void PdfView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    rebuildLayout();
    if (fitMode == 1)
        fitWidth();
    else if (fitMode == 2)
        fitPage();
    else
        schedule();
}
void PdfView::scrollContentsBy(int, int) {
    if (!changingLayout)
        schedule();
    viewport()->update();
}
void PdfView::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        fitMode = 0;
        const double steps = e->angleDelta().y() / 120.;
        setZoom(scale * std::pow(1.15, steps), e->position().toPoint());
        e->accept();
    } else
        QAbstractScrollArea::wheelEvent(e);
}
void PdfView::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton) {
        dragging = true;
        dragPosition = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
    } else
        QAbstractScrollArea::mousePressEvent(e);
}
void PdfView::mouseMoveEvent(QMouseEvent *e) {
    if (dragging) {
        const auto d = e->pos() - dragPosition;
        dragPosition = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
    } else
        QAbstractScrollArea::mouseMoveEvent(e);
}
void PdfView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton) {
        dragging = false;
        viewport()->unsetCursor();
        e->accept();
    } else
        QAbstractScrollArea::mouseReleaseEvent(e);
}
void PdfView::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && !e->mimeData()->urls().isEmpty() &&
        e->mimeData()->urls().first().isLocalFile())
        e->acceptProposedAction();
}
void PdfView::dropEvent(QDropEvent *e) {
    if (e->mimeData()->hasUrls() && !e->mimeData()->urls().isEmpty()) {
        emit fileDropped(e->mimeData()->urls().first().toLocalFile());
        e->acceptProposedAction();
    }
}
bool PdfView::event(QEvent *e) {
    const bool result = QAbstractScrollArea::event(e);
    if (e->type() == QEvent::ScreenChangeInternal) {
        schedule();
        viewport()->update();
    }
    return result;
}
} // namespace airlet
