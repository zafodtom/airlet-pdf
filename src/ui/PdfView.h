#pragma once
#include "render/RenderService.h"
#include "render/TileCache.h"
#include <QAbstractScrollArea>
#include <QCache>
#include <QElapsedTimer>
#include <memory>

namespace airlet {
class PdfView final : public QAbstractScrollArea {
    Q_OBJECT
  public:
    enum class SelectionMode { Text, Area };
    explicit PdfView(QWidget *parent = nullptr);
    void openFile(const QString &path, const QString &password = {});
    void closeDocument();
    void goToPage(int page);
    int currentPage() const;
    int pageCount() const {
        return sizes.size();
    }
    double zoom() const {
        return scale;
    }
    void setZoom(double value, QPoint anchor = {-1, -1});
    void fitWidth();
    void fitPage();
    void rotate(int delta);
    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const {
        return mode;
    }
    void clearSelection();
    QString selectedText() const;
    bool hasAreaSelection() const;
    quint64 requestSelectionImage();
    void cancelSelectionImage();
    QPointF pageToViewport(int page, QPointF point) const;
    QPointF viewportToPage(int page, QPointF point) const;
    qint64 cachedBytes() const {
        return sharp.bytes() + previews.bytes();
    }
    quint64 jobsStarted() const {
        return service.jobsStarted();
    }
    quint64 jobsFinished() const {
        return service.jobsFinished();
    }
  signals:
    void documentOpened(QString path, int pages);
    void openFailed(QString path, airlet::OpenResult result);
    void pageChanged(int page, int total);
    void zoomChanged(double scale);
    void firstImage(qint64 openMs);
    void fileDropped(QString path);
    void selectionChanged();
    void selectionNotice(QString message);
    void selectionImageReady(quint64 request, QImage image, QString error);
    void selectionImageProgress(quint64 request, int percent);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;

  private:
    RenderService service;
    TileCache sharp{96}, previews{16};
    quint64 session = 0, generation = 0;
    QVector<QSizeF> sizes;
    QVector<int> rotations;
    QVector<QRectF> pages;
    double scale = 1.; // 100% = 96 logical dpi / 72 pdf points
    double logicalScale() const {
        return scale * 96. / 72.;
    }
    QString path;
    QElapsedTimer openingTimer;
    bool firstPending = false, changingLayout = false, dragging = false, metadataPending = false;
    QPoint dragPosition;
    int pageWheelRemainder = 0;
    double pagePixelRemainder = 0;
    SelectionMode mode = SelectionMode::Text;
    int selectionPage = -1, firstGlyph = -1, lastGlyph = -1;
    QPointF selectionStart, selectionEnd;
    bool selecting = false;
    QCache<int, TextPage> textCache{4};
    std::shared_ptr<TextPage> activeText;
    quint64 textRequest = 0, imageRequest = 0;
    int pageAtPoint(QPointF viewportPoint) const;
    void updateTextSelection();
    QRectF areaBounds() const;
    int fitMode = 1; // 0 manual, 1 width, 2 page
    double documentWidth = 0;
    QRectF pageRect(int index) const;
    int pageAtY(double y) const;
    QSize pixelSize(int index) const;
    QString tileKey(int page, QSize size, QRect tile) const;
    QString previewKey(int page) const;
    void rebuildLayout();
    void schedule();
    void acceptResult(quint64 s, quint64 g, RenderRequest request, PageInfo info, QImage image,
                      qint64 ms);
};
} // namespace airlet
