#pragma once
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace airlet {
// All geometry is in displayed, crop-relative points, top-left origin. Intrinsic
// PDF page rotation is already applied. UI rotation is passed separately.
struct PageInfo {
    QSizeF points;
};
struct Glyph {
    QString text;
    QRectF bounds;
};
struct TextPage {
    QVector<Glyph> glyphs;
    bool copyAllowed = false;
};
enum class OpenError { None, Password, Invalid, Io };
struct OpenResult {
    OpenError error = OpenError::None;
    QString message;
    int pages = 0;
    PageInfo first;
};
struct RenderRequest {
    int page = 0;
    int rotation = 0; // additional clockwise quarter turns
    QSize fullPixels;
    QRect tile; // pixels in rotated full page, no devicePixelRatio attached
    bool preview = false;
    QString key;
};
class PdfEngine {
  public:
    virtual ~PdfEngine() = default;
    virtual OpenResult open(const QString &path, const QString &password) = 0;
    virtual void close() = 0;
    virtual PageInfo pageInfo(int page) = 0;
    virtual QImage render(const RenderRequest &, const std::function<bool()> &cancelled) = 0;
    virtual TextPage text(int page) = 0;
    virtual int printDpi() {
        return 0;
    }
    // Image extraction is intentionally capability-gated until milestone 3.
    virtual bool supportsImageExtraction() const {
        return false;
    }
};
std::unique_ptr<PdfEngine> makePdfiumEngine();
} // namespace airlet
