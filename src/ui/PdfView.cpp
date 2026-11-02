#include "PdfView.h"
#include "core/Geometry.h"
#include "core/Selection.h"
#include "i18n/Language.h"
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QKeyEvent>
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
            emit openFailed(path, {OpenError::Invalid,
                                   uiText(QStringLiteral("Neplatné rozměry první stránky.")),
                                   0,
                                   {}});
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
    connect(&service, &RenderService::textReady, this,
            [this](quint64 s, quint64 request, int p, TextPage text) {
                if (s != session)
                    return;
                textCache.insert(p, new TextPage(text));
                if (request != textRequest || p != selectionPage || mode != SelectionMode::Text)
                    return;
                activeText = std::make_shared<TextPage>(std::move(text));
                updateTextSelection();
                if (!activeText->copyAllowed)
                    emit selectionNotice(
                        uiText(QStringLiteral("Dokument nepovoluje kopírování textu.")));
                else if (activeText->glyphs.isEmpty())
                    emit selectionNotice(
                        uiText(QStringLiteral("Na stránce není textová vrstva. Použijte Výřez.")));
            });
    connect(&service, &RenderService::areaReady, this,
            [this](quint64 s, quint64 request, QImage image, QString error) {
                if (s == session && request == imageRequest)
                    emit selectionImageReady(request, image, error);
            });
    connect(&service, &RenderService::areaProgress, this,
            [this](quint64 s, quint64 request, int percent) {
                if (s == session && request == imageRequest)
                    emit selectionImageProgress(request, percent);
            });
}
void PdfView::openFile(const QString &p, const QString &password) {
    closeDocument();
    path = p;
    this->password = password;
    openingTimer.start();
    service.open(session, path, password);
}
void PdfView::closeDocument() {
    clearSelection();
    clearSearchHit();
    cancelSelectionImage();
    textCache.clear();
    password.clear();
    ++session;
    service.close(session);
    boundaryWheel = 0;
    boundaryPixels = 0;
    pageWheelRemainder = 0;
    pagePixelRemainder = 0;
    lastPageReady = false;
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
    verticalScrollBar()->setSingleStep(std::clamp(viewport()->height() / 20, 32, 64));
    horizontalScrollBar()->setSingleStep(40);
    changingLayout = false;
}
void PdfView::setContinueToNextDocument(bool enabled) {
    continueDocuments = enabled;
    boundaryWheel = 0;
    boundaryPixels = 0;
    if (!enabled)
        navigationHeld = false;
}
void PdfView::suppressNavigationInput() {
    navigationHeld = true;
    continuationCooldown.start();
    lastForwardInput.start();
    boundaryWheel = 0;
    boundaryPixels = 0;
}
bool PdfView::navigationBlocked() {
    if (!navigationHeld)
        return false;
    const bool blocked = continuationCooldown.elapsed() < 600 || lastForwardInput.elapsed() < 250;
    lastForwardInput.restart();
    if (!blocked)
        navigationHeld = false;
    return blocked;
}
void PdfView::requestNextDocument() {
    if (!continueDocuments || pages.isEmpty() || firstPending || !lastPageReady)
        return;
    suppressNavigationInput();
    emit nextDocumentRequested();
}
void PdfView::nextPage() {
    if (navigationBlocked())
        return;
    if (!pages.isEmpty() && (currentPage() == pages.size() - 1 ||
                             verticalScrollBar()->value() == verticalScrollBar()->maximum()))
        requestNextDocument();
    else
        goToPage(currentPage() + 1);
}
void PdfView::goToPage(int p) {
    if (pages.isEmpty())
        return;
    p = std::clamp(p, 0, int(pages.size() - 1));
    if (fitMode == 2) {
        const auto size = rotatedSize(sizes[p], rotations[p]);
        const auto nextScale =
            std::clamp(std::min((viewport()->width() - 2 * gap) / (size.width() * 96. / 72.),
                                (viewport()->height() - 2 * gap) / (size.height() * 96. / 72.)),
                       .05, 16.);
        if (std::abs(nextScale - scale) > 1e-8)
            setZoom(nextScale);
        fitMode = 2;
    }
    verticalScrollBar()->setValue(qRound(pages[p].top() - gap));
    schedule();
}
void PdfView::setZoom(double value, QPoint anchor) {
    fitMode = 0;
    pageWheelRemainder = 0;
    pagePixelRemainder = 0;
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
    if (r.page == pages.size() - 1)
        lastPageReady = true;
    if (r.preview)
        previews.put(r.key, image);
    else
        sharp.put(r.key, image);
    viewport()->update(); // paint never submits work
}
void PdfView::clearSearchHit() {
    searchPage = -1;
    searchBounds.clear();
    viewport()->update();
}
void PdfView::showSearchHit(int p, QVector<QRectF> bounds, QSizeF points) {
    if (p < 0 || p >= pages.size() || bounds.isEmpty())
        return;
    if (points.isValid() && sizes[p] != points) {
        sizes[p] = points;
        rebuildLayout();
    }
    searchPage = p;
    searchBounds = highlightBands(bounds);
    if (searchBounds.isEmpty()) {
        clearSearchHit();
        return;
    }
    goToPage(p);
    const auto rect = rotateRect(searchBounds.first(), sizes[p], rotations[p]);
    const auto point = rect.center() * logicalScale() + pages[p].topLeft();
    verticalScrollBar()->setValue(qRound(point.y() - viewport()->height() / 2.));
    horizontalScrollBar()->setValue(qRound(point.x() - viewport()->width() / 2.));
    viewport()->update();
}
void PdfView::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), QColor(66, 69, 73));
    if (pages.isEmpty()) {
        painter.setPen(QColor(225, 225, 225));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         uiText(QStringLiteral("Otevřete PDF nebo jej přetáhněte sem")));
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
    if (searchPage >= 0 && searchPage < pages.size()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 165, 0, 135));
        for (const auto &bounds : searchBounds) {
            const auto rect = rotateRect(bounds, sizes[searchPage], rotations[searchPage]);
            painter.drawRect(
                QRectF(rect.topLeft() * logicalScale() + pages[searchPage].topLeft() - offset,
                       rect.size() * logicalScale()));
        }
    }
    if (selectionPage >= 0 && selectionPage < pages.size()) {
        painter.setPen(QPen(QColor(25, 110, 220), 1));
        painter.setBrush(QColor(35, 125, 240, 65));
        const auto transform = [this, offset](QRectF bounds) {
            return QRectF(
                rotateRect(bounds, sizes[selectionPage], rotations[selectionPage]).topLeft() *
                        logicalScale() +
                    pages[selectionPage].topLeft() - offset,
                rotateRect(bounds, sizes[selectionPage], rotations[selectionPage]).size() *
                    logicalScale());
        };
        if (mode == SelectionMode::Area && hasAreaSelection())
            painter.drawRect(transform(areaBounds()));
        else if (activeText && firstGlyph >= 0 && lastGlyph >= 0) {
            painter.setPen(Qt::NoPen);
            auto highlight = palette().color(QPalette::Highlight);
            highlight.setAlpha(100);
            painter.setBrush(highlight);
            for (const auto &band : selectionBands)
                painter.drawRect(transform(band));
        }
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
        const double steps =
            !e->pixelDelta().isNull() ? e->pixelDelta().y() / 120. : e->angleDelta().y() / 120.;
        setZoom(scale * std::pow(1.15, steps), e->position().toPoint());
        e->accept();
    } else if ((e->pixelDelta().y() < 0 || (e->pixelDelta().isNull() && e->angleDelta().y() < 0)) &&
               navigationBlocked()) {
        e->accept();
    } else if (continueDocuments && !pages.isEmpty() && fitMode != 2 &&
               !(e->modifiers() & Qt::ShiftModifier) &&
               verticalScrollBar()->value() == verticalScrollBar()->maximum() &&
               (e->pixelDelta().y() < 0 || (e->pixelDelta().isNull() && e->angleDelta().y() < 0))) {
        if (e->phase() != Qt::ScrollMomentum) {
            if (e->phase() == Qt::ScrollBegin) {
                boundaryWheel = 0;
                boundaryPixels = 0;
            }
            if (!e->pixelDelta().isNull())
                boundaryPixels -= e->pixelDelta().y();
            else
                boundaryWheel -= e->angleDelta().y();
            if (boundaryWheel >= 120 || boundaryPixels >= 80)
                requestNextDocument();
            if (e->phase() == Qt::ScrollEnd) {
                boundaryWheel = 0;
                boundaryPixels = 0;
            }
        }
        e->accept();
    } else if (fitMode == 2 && !pages.isEmpty() && !(e->modifiers() & Qt::ShiftModifier) &&
               (e->angleDelta().y() != 0 || e->pixelDelta().y() != 0)) {
        int steps = 0;
        if (!e->pixelDelta().isNull()) {
            if (e->phase() == Qt::ScrollBegin)
                pagePixelRemainder = 0;
            pagePixelRemainder += e->pixelDelta().y();
            const auto threshold = std::clamp(viewport()->height() * .25, 80., 200.);
            steps = int(pagePixelRemainder / threshold);
            pagePixelRemainder -= steps * threshold;
            if (e->phase() == Qt::ScrollEnd)
                pagePixelRemainder = 0;
        } else {
            pageWheelRemainder += e->angleDelta().y();
            steps = pageWheelRemainder / 120;
            pageWheelRemainder %= 120;
        }
        if (steps < 0 &&
            (currentPage() == pages.size() - 1 ||
             verticalScrollBar()->value() == verticalScrollBar()->maximum()) &&
            e->phase() != Qt::ScrollMomentum)
            requestNextDocument();
        else if (steps)
            goToPage(currentPage() - steps);
        e->accept();
    } else if (!e->pixelDelta().isNull()) {
        auto delta = e->pixelDelta();
        if ((e->modifiers() & Qt::ShiftModifier) && delta.x() == 0) {
            delta.setX(delta.y());
            delta.setY(0);
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
    } else
        QAbstractScrollArea::wheelEvent(e);
}
void PdfView::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    if (e->button() == Qt::MiddleButton) {
        dragging = true;
        dragPosition = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
    } else if (e->button() == Qt::LeftButton) {
        clearSelection();
        selectionPage = pageAtPoint(e->position());
        if (selectionPage < 0)
            return;
        selecting = true;
        selectionStart = selectionEnd = viewportToPage(selectionPage, e->position());
        if (mode == SelectionMode::Text) {
            if (const auto *text = textCache.object(selectionPage)) {
                activeText = std::make_shared<TextPage>(*text);
                updateTextSelection();
            } else
                service.requestText(session, selectionPage, textRequest);
        }
        viewport()->update();
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
    } else if (selecting) {
        selectionEnd = viewportToPage(selectionPage, e->position());
        updateTextSelection();
        viewport()->update();
        emit selectionChanged();
        e->accept();
    } else
        QAbstractScrollArea::mouseMoveEvent(e);
}
void PdfView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton) {
        dragging = false;
        viewport()->setCursor(mode == SelectionMode::Area ? Qt::CrossCursor : Qt::IBeamCursor);
        e->accept();
    } else if (e->button() == Qt::LeftButton && selecting) {
        selectionEnd = viewportToPage(selectionPage, e->position());
        selecting = false;
        updateTextSelection();
        viewport()->update();
        emit selectionChanged();
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
int PdfView::pageAtPoint(QPointF point) const {
    point += QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const int p = pageAtY(point.y());
    return p >= 0 && pages[p].contains(point) ? p : -1;
}
QPointF PdfView::pageToViewport(int p, QPointF point) const {
    if (p < 0 || p >= pages.size())
        return {};
    return pages[p].topLeft() + rotatePoint(point, sizes[p], rotations[p]) * logicalScale() -
           QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
}
QPointF PdfView::viewportToPage(int p, QPointF point) const {
    if (p < 0 || p >= pages.size())
        return {};
    auto rotated = (point + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()) -
                    pages[p].topLeft()) /
                   logicalScale();
    auto result = unrotatePoint(rotated, sizes[p], rotations[p]);
    return {std::clamp(result.x(), 0., sizes[p].width()),
            std::clamp(result.y(), 0., sizes[p].height())};
}
void PdfView::setSelectionMode(SelectionMode value) {
    clearSelection();
    mode = value;
    viewport()->setCursor(mode == SelectionMode::Area ? Qt::CrossCursor : Qt::IBeamCursor);
}
void PdfView::clearSelection() {
    ++textRequest;
    selecting = false;
    selectionPage = -1;
    firstGlyph = lastGlyph = -1;
    activeText.reset();
    selectionBands.clear();
    selectionStart = selectionEnd = {};
    viewport()->update();
    emit selectionChanged();
}
void PdfView::updateTextSelection() {
    if (mode != SelectionMode::Text || !activeText || !activeText->copyAllowed)
        return;
    firstGlyph = nearestGlyph(*activeText, selectionStart);
    lastGlyph = nearestGlyph(*activeText, selectionEnd);
    QVector<QRectF> bounds;
    if (firstGlyph >= 0 && lastGlyph >= 0) {
        for (int i = std::min(firstGlyph, lastGlyph); i <= std::max(firstGlyph, lastGlyph); ++i) {
            const auto &glyph = activeText->glyphs[i];
            if (glyph.text.contains('\r') || glyph.text.contains('\n'))
                bounds.append(QRectF());
            else if (!glyph.bounds.isEmpty())
                bounds.append(glyph.bounds);
        }
    }
    selectionBands = highlightBands(bounds);
    viewport()->update();
    emit selectionChanged();
}
QString PdfView::selectedText() const {
    return mode == SelectionMode::Text && activeText
               ? airlet::selectedText(*activeText, firstGlyph, lastGlyph)
               : QString();
}
QRectF PdfView::areaBounds() const {
    if (selectionPage < 0 || selectionPage >= sizes.size())
        return {};
    return QRectF(selectionStart, selectionEnd)
        .normalized()
        .intersected(QRectF(QPointF(), sizes[selectionPage]));
}
bool PdfView::hasAreaSelection() const {
    return mode == SelectionMode::Area && areaBounds().width() > 1. && areaBounds().height() > 1.;
}
quint64 PdfView::requestSelectionImage() {
    if (!hasAreaSelection())
        return 0;
    const auto points = rotateRect(areaBounds(), sizes[selectionPage], rotations[selectionPage]);
    const auto pageSize = rotatedSize(sizes[selectionPage], rotations[selectionPage]);
    // 144 dpi by default; bound a single interactive crop to 16 megapixels.
    const double k =
        std::min({2., 8192. / points.width(), 8192. / points.height(),
                  std::sqrt((16. * 1024 * 1024 - 32768) / (points.width() * points.height()))});
    const QSize full(qCeil(pageSize.width() * k), qCeil(pageSize.height() * k));
    const QRect tile = QRectF(points.topLeft() * k, points.size() * k)
                           .toAlignedRect()
                           .intersected(QRect(QPoint(), full));
    ++imageRequest;
    service.requestArea(session, imageRequest,
                        {selectionPage, rotations[selectionPage], full, tile, false, {}});
    return imageRequest;
}
void PdfView::cancelSelectionImage() {
    ++imageRequest;
    service.cancelArea();
}
void PdfView::keyPressEvent(QKeyEvent *e) {
    if (e->matches(QKeySequence::Copy) && !selectedText().isEmpty()) {
        QApplication::clipboard()->setText(selectedText());
        e->accept();
    } else if (e->key() == Qt::Key_Escape) {
        clearSelection();
        e->accept();
    } else
        QAbstractScrollArea::keyPressEvent(e);
}
} // namespace airlet
