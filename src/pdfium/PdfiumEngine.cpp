#include "PdfiumEngine.h"
#include <QCache>
#include <QFile>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <fpdf_progressive.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#include <mutex>

namespace airlet {
namespace {
// Global guard also covers tests/any future accidental second engine instance.
// Export will use a separate process, never another concurrent PDFium thread.
std::recursive_mutex apiMutex;
int libraryUsers = 0;
struct Page {
    FPDF_PAGE handle;
    ~Page() {
        if (handle)
            FPDF_ClosePage(handle);
    }
};
struct Pause {
    IFSDK_PAUSE api{};
    const std::function<bool()> *cancelled;
    static FPDF_BOOL check(IFSDK_PAUSE *p) {
        return (*static_cast<Pause *>(p->user)->cancelled)();
    }
    explicit Pause(const std::function<bool()> &fn) : cancelled(&fn) {
        api.version = 1;
        api.user = this;
        api.NeedToPauseNow = &check;
    }
};
class PdfiumEngine final : public PdfEngine {
    QFile file;
    FPDF_FILEACCESS access{};
    FPDF_DOCUMENT doc = nullptr;
    QCache<int, Page> loadedPages{3};
    FPDF_PAGE loadPage(int index) {
        if (auto *p = loadedPages.object(index))
            return p->handle;
        auto *p = new Page{FPDF_LoadPage(doc, index)};
        if (!p->handle) {
            delete p;
            return nullptr;
        }
        const auto handle = p->handle;
        loadedPages.insert(index, p);
        return handle;
    }
    static int read(void *context, unsigned long offset, unsigned char *buffer,
                    unsigned long size) {
        auto *f = static_cast<QFile *>(context);
        return f->seek(offset) &&
               f->read(reinterpret_cast<char *>(buffer), size) == static_cast<qint64>(size);
    }

  public:
    PdfiumEngine() {
        std::lock_guard lock(apiMutex);
        if (libraryUsers++ == 0) {
            FPDF_LIBRARY_CONFIG c{};
            c.version = 2;
            FPDF_InitLibraryWithConfig(&c);
        }
    }
    ~PdfiumEngine() override {
        std::lock_guard lock(apiMutex);
        close();
        if (--libraryUsers == 0)
            FPDF_DestroyLibrary();
    }
    OpenResult open(const QString &path, const QString &password) override {
        std::lock_guard lock(apiMutex);
        close();
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 0xffffffffLL)
            return {
                OpenError::Io,
                QStringLiteral("Soubor nelze číst nebo přesahuje limit 4 GiB PDFium FILEACCESS."),
                0,
                {}};
        access.m_FileLen = static_cast<unsigned long>(file.size());
        access.m_Param = &file;
        access.m_GetBlock = &read;
        const auto pw = password.toUtf8();
        doc = FPDF_LoadCustomDocument(&access, pw.isEmpty() ? nullptr : pw.constData());
        if (!doc) {
            const auto e = FPDF_GetLastError();
            file.close();
            return {e == FPDF_ERR_PASSWORD ? OpenError::Password : OpenError::Invalid,
                    e == FPDF_ERR_PASSWORD ? QStringLiteral("PDF vyžaduje správné heslo.")
                                           : QStringLiteral("PDF nelze otevřít (chyba %1).").arg(e),
                    0,
                    {}};
        }
        const int n = FPDF_GetPageCount(doc);
        if (n <= 0) {
            close();
            return {OpenError::Invalid, QStringLiteral("PDF nemá žádné stránky."), 0, {}};
        }
        return {OpenError::None, {}, n, pageInfo(0)};
    }
    void close() override {
        std::lock_guard lock(apiMutex);
        loadedPages.clear();
        if (doc) {
            FPDF_CloseDocument(doc);
            doc = nullptr;
        }
        file.close();
    }
    PageInfo pageInfo(int page) override {
        std::lock_guard lock(apiMutex);
        FS_SIZEF s{};
        if (!doc || !FPDF_GetPageSizeByIndexF(doc, page, &s) || s.width <= 0 || s.height <= 0 ||
            !std::isfinite(s.width) || !std::isfinite(s.height))
            return {};
        return {QSizeF(s.width, s.height)};
    }
    QImage render(const RenderRequest &r, const std::function<bool()> &cancelled) override {
        std::lock_guard lock(apiMutex);
        if (!doc || cancelled() || r.tile.isEmpty() || r.tile.width() > 2048 ||
            r.tile.height() > 2048 || r.fullPixels.isEmpty() ||
            !QRect(QPoint(), r.fullPixels).contains(r.tile))
            return {};
        auto page = loadPage(r.page);
        if (!page || cancelled())
            return {};
        QImage image(r.tile.size(), QImage::Format_ARGB32);
        if (image.isNull())
            return {};
        image.fill(Qt::white);
        auto bitmap = FPDFBitmap_CreateEx(image.width(), image.height(), FPDFBitmap_BGRA,
                                          image.bits(), image.bytesPerLine());
        if (!bitmap)
            return {};
        Pause pause(cancelled);
        int state = FPDF_RenderPageBitmap_Start(bitmap, page, -r.tile.x(), -r.tile.y(),
                                                r.fullPixels.width(), r.fullPixels.height(),
                                                r.rotation, FPDF_ANNOT, &pause.api);
        while (state == FPDF_RENDER_TOBECONTINUED && !cancelled())
            state = FPDF_RenderPage_Continue(page, &pause.api);
        FPDF_RenderPage_Close(page);
        FPDFBitmap_Destroy(bitmap);
        if (cancelled() || state != FPDF_RENDER_DONE)
            return {};
        return image;
    }
    TextPage text(int index) override {
        std::lock_guard lock(apiMutex);
        TextPage result;
        if (!doc || !(FPDF_GetDocPermissions(doc) & (1u << 4)))
            return result;
        result.copyAllowed = true;
        auto page = loadPage(index);
        if (!page)
            return result;
        auto text = FPDFText_LoadPage(page);
        if (!text)
            return result;
        // PDFium performs the intrinsic-rotation/crop conversion. Use a large
        // logical device rectangle and normalize back to points.
        const auto s = pageInfo(index).points;
        const int w = std::max(1, qRound(s.width() * 100)),
                  h = std::max(1, qRound(s.height() * 100));
        const int count = FPDFText_CountChars(text);
        for (int i = 0; i < count; ++i) {
            const char32_t cp = FPDFText_GetUnicode(text, i);
            if (!cp)
                continue;
            double l = 0, r = 0, b = 0, t = 0;
            if (!FPDFText_GetCharBox(text, i, &l, &r, &b, &t))
                continue;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            FPDF_PageToDevice(page, 0, 0, w, h, 0, l, b, &x1, &y1);
            FPDF_PageToDevice(page, 0, 0, w, h, 0, r, t, &x2, &y2);
            result.glyphs.push_back(
                {QString::fromUcs4(&cp, 1),
                 QRectF(QPointF(x1 / 100., y1 / 100.), QPointF(x2 / 100., y2 / 100.))
                     .normalized()});
        }
        FPDFText_ClosePage(text);
        return result;
    }
};
} // namespace
std::unique_ptr<PdfEngine> makePdfiumEngine() {
    return std::make_unique<PdfiumEngine>();
}
} // namespace airlet
