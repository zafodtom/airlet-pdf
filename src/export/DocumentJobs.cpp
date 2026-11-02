#include "DocumentJobs.h"
#include "PageExport.h"
#include "core/Geometry.h"
#include "core/PdfEngine.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace airlet {
static void report(const QJsonObject &o) {
    const auto bytes = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    std::fflush(stdout);
}
static int fail(const QString &message) {
    report({{"error", message}});
    return 1;
}
static QRectF fitRect(QSizeF size, QRectF area) {
    size.scale(area.size(), Qt::KeepAspectRatio);
    return {area.center() - QPointF(size.width() / 2, size.height() / 2), size};
}
static int imagesPdf(const QJsonObject &job) {
    const auto files = job["images"].toArray();
    const auto target = job["output"].toString();
    const int dpi = job["dpi"].toInt(150), paper = job["paper"].toInt();
    if (files.isEmpty() || files.size() > 10000 || dpi < 72 || dpi > 600 || paper < 0 ||
        paper > 2 || target.isEmpty())
        return fail(QStringLiteral("Neplatné parametry obrázků."));
    if (QFileInfo::exists(target))
        return fail(QStringLiteral("Výstup již existuje. Zvolte nový název."));
    QTemporaryFile file(QDir(job["temporary"].toString()).filePath("images-XXXXXX.part"));
    if (!file.open())
        return fail(file.errorString());
    QImageReader::setAllocationLimit(256);
    {
        QPdfWriter writer(&file);
        writer.setCreator(QStringLiteral("Airlet PDF"));
        writer.setTitle(QFileInfo(target).completeBaseName());
        writer.setResolution(150);
        QPainter painter;
        for (int i = 0; i < files.size(); ++i) {
            QImageReader reader(files[i].toString());
            reader.setAutoTransform(true);
            const auto size = reader.size();
            if (!size.isValid() || size.width() > 32768 || size.height() > 32768 ||
                qint64(size.width()) * size.height() > 64LL * 1024 * 1024)
                return fail(
                    QStringLiteral("Obrázek %1 má neplatné rozměry nebo přesahuje 64 megapixelů.")
                        .arg(i + 1));
            auto image = reader.read();
            if (image.isNull())
                return fail(QStringLiteral("Obrázek %1 nelze načíst: %2")
                                .arg(i + 1)
                                .arg(reader.errorString()));
            const QPageSize pageSize =
                paper == 0 ? QPageSize(QSizeF(image.size()) * (72. / dpi), QPageSize::Point)
                           : QPageSize(QPageSize::A4);
            const auto orientation = paper == 2 ? QPageLayout::Landscape : QPageLayout::Portrait;
            const auto margin = paper == 0 ? 0. : 10.;
            if (!writer.setPageLayout(QPageLayout(pageSize, orientation,
                                                  QMarginsF(margin, margin, margin, margin),
                                                  QPageLayout::Millimeter)))
                return fail(QStringLiteral("Nelze nastavit velikost stránky."));
            if (i == 0) {
                if (!painter.begin(&writer))
                    return fail(QStringLiteral("Nelze vytvořit PDF."));
            } else if (!writer.newPage())
                return fail(QStringLiteral("Nelze přidat stránku PDF."));
            const QRectF area(0, 0, writer.width(), writer.height());
            painter.fillRect(area, Qt::white);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(fitRect(image.size(), area), image);
            report({{"completed", i + 1}, {"total", files.size()}});
        }
        if (!painter.end())
            return fail(QStringLiteral("PDF se nepodařilo dokončit."));
    }
    if (!file.flush())
        return fail(file.errorString());
    if (!file.rename(target))
        return fail(file.errorString());
    file.setAutoRemove(false);
    report({{"success", true}});
    return 0;
}
static int searchPdf(PdfEngine &engine, int count, const QJsonObject &job) {
    const auto query = job["query"].toString().simplified();
    if (query.isEmpty() || query.size() > 256)
        return fail(QStringLiteral("Zadejte nejvýše 256 znaků."));
    int hits = 0, textPages = 0;
    for (int p = 0; p < count; ++p) {
        const auto page = engine.text(p);
        if (!page.copyAllowed)
            return fail(QStringLiteral("Dokument nepovoluje získání textu."));
        QString text;
        QVector<int> glyphs;
        for (int g = 0; g < page.glyphs.size(); ++g) {
            for (const auto ch : page.glyphs[g].text) {
                if (ch.isSpace()) {
                    if (text.isEmpty() || text.back() == QChar(' '))
                        continue;
                    text += QChar(' ');
                } else
                    text += ch;
                glyphs.append(g);
            }
        }
        if (!text.isEmpty())
            ++textPages;
        QJsonArray matches;
        for (int start = 0; (start = text.indexOf(query, start, Qt::CaseInsensitive)) >= 0;
             start += query.size()) {
            QJsonArray rects;
            for (int g = glyphs[start]; g <= glyphs[start + query.size() - 1]; ++g) {
                const auto r = page.glyphs[g].bounds;
                if (!r.isEmpty())
                    rects.append(QJsonArray{r.x(), r.y(), r.width(), r.height()});
            }
            if (!rects.isEmpty()) {
                matches.append(
                    QJsonObject{{"page", p},
                                {"rects", rects},
                                {"size", QJsonArray{engine.pageInfo(p).points.width(),
                                                    engine.pageInfo(p).points.height()}}});
                ++hits;
            }
            if (hits >= 2000)
                break;
        }
        report({{"matches", matches}, {"completed", p + 1}, {"total", count}});
        if (hits >= 2000)
            break;
    }
    report({{"success", true}, {"limited", hits >= 2000}, {"textPages", textPages}});
    return 0;
}
static int printPdf(PdfEngine &engine, int count, const QJsonObject &job) {
    if (engine.printDpi() == 0)
        return fail(QStringLiteral("Dokument nepovoluje tisk."));
    QString error;
    auto pages = parsePageRange(job["range"].toString(), count, &error);
    if (!error.isEmpty())
        return fail(error);
    if (job["reverse"].toBool())
        std::reverse(pages.begin(), pages.end());
    auto printerOwner = std::make_unique<QPrinter>(QPrinter::HighResolution);
    auto &printer = *printerOwner;
    const bool pdf = job["pdf"].toBool(); // deterministic PDF backend also used in tests
    const auto target = job["output"].toString();
    QTemporaryFile file(QDir(job["temporary"].toString()).filePath("print-XXXXXX.pdf"));
    if (pdf) {
        if (target.isEmpty() || QFileInfo::exists(target))
            return fail(QStringLiteral("Výstup již existuje nebo není zadán."));
        if (!file.open())
            return fail(file.errorString());
        file.setAutoRemove(false);
        const auto tempPath = file.fileName();
        // Destroy the temporary-file handle before QPrinter opens this path on Windows.
        file.remove();
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(tempPath);
    } else {
        printer.setPrinterName(job["printer"].toString());
        if (!printer.isValid())
            return fail(QStringLiteral("Tiskárna není dostupná."));
    }
    const auto mm = job["paperMm"].toArray();
    if (mm.size() != 2)
        return fail(QStringLiteral("Neplatný formát papíru."));
    printer.setPageSize(
        QPageSize(QSizeF(mm[0].toDouble(), mm[1].toDouble()), QPageSize::Millimeter));
    printer.setPageOrientation(QPageLayout::Portrait); // dimensions already include orientation
    const auto margins = job["marginsMm"].toArray();
    if (margins.size() == 4)
        printer.setPageMargins(QMarginsF(margins[0].toDouble(), margins[1].toDouble(),
                                         margins[2].toDouble(), margins[3].toDouble()),
                               QPageLayout::Millimeter);
    printer.setFullPage(false);
    printer.setResolution(std::clamp(job["resolution"].toInt(300), 72, 1200));
    printer.setDocName(QFileInfo(job["source"].toString()).fileName());
    printer.setColorMode(job["gray"].toBool() ? QPrinter::GrayScale : QPrinter::Color);
    printer.setDuplex(QPrinter::DuplexMode(job["duplex"].toInt()));
    printer.setPaperSource(QPrinter::PaperSource(job["paperSource"].toInt(QPrinter::Auto)));
    const int copies = std::clamp(job["copies"].toInt(1), 1, 99);
    printer.setCopyCount(printer.supportsMultipleCopies() ? copies : 1);
    printer.setCollateCopies(job["collate"].toBool());
    const int manualCopies = printer.supportsMultipleCopies() ? 1 : copies;
    QVector<int> sequence;
    if (job["collate"].toBool())
        for (int c = 0; c < manualCopies; ++c)
            sequence += pages;
    else
        for (int p : pages)
            for (int c = 0; c < manualCopies; ++c)
                sequence.append(p);
    QPainter painter;
    if (!painter.begin(&printer))
        return fail(QStringLiteral("Tisk se nepodařilo zahájit."));
    const auto rotations = job["rotations"].toArray();
    for (int i = 0; i < sequence.size(); ++i) {
        if (i && !printer.newPage()) {
            printer.abort();
            return fail(QStringLiteral("Tisk stránky selhal."));
        }
        const int p = sequence[i], rotation = (p < rotations.size() ? rotations[p].toInt() : 0);
        const auto size = rotatedSize(engine.pageInfo(p).points, rotation);
        const auto full = QSize(int(std::ceil(size.width() * engine.printDpi() / 72.)),
                                int(std::ceil(size.height() * engine.printDpi() / 72.)));
        if (full.width() <= 0 || full.height() <= 0 || full.width() > 32768 ||
            full.height() > 32768) {
            printer.abort();
            return fail(QStringLiteral("Stránka je pro tisk příliš velká."));
        }
        const auto area = printer.pageRect(QPrinter::DevicePixel);
        const auto dest = fitRect(size, QRectF(0, 0, area.width(), area.height()));
        for (int y = 0; y < full.height(); y += 512)
            for (int x = 0; x < full.width(); x += 512) {
                const auto tile = QRect(x, y, 512, 512).intersected(QRect(QPoint(), full));
                const auto image =
                    engine.render({p, rotation, full, tile, false, {}}, [] { return false; });
                if (image.isNull()) {
                    printer.abort();
                    return fail(QStringLiteral("Stránku nelze vykreslit pro tisk."));
                }
                painter.drawImage(QRectF(dest.x() + x * dest.width() / full.width(),
                                         dest.y() + y * dest.height() / full.height(),
                                         tile.width() * dest.width() / full.width(),
                                         tile.height() * dest.height() / full.height()),
                                  image);
            }
        report({{"completed", i + 1}, {"total", sequence.size()}});
    }
    if (!painter.end() || printer.printerState() == QPrinter::Error)
        return fail(QStringLiteral("Tisk nebyl dokončen."));
    const auto printedPath = printer.outputFileName();
    printerOwner.reset(); // close the native output handle before finalizing on Windows
    if (pdf) {
        // QPrinter has closed its output. No copy/delete fallback or overwrite.
        // Rename the closed native file in the same destination filesystem.
        if (!QFile::rename(printedPath, target))
            return fail(QStringLiteral("PDF nelze dokončit."));
    }
    report({{"success", true}});
    return 0;
}
int runDocumentJob(const QJsonObject &job) {
    const auto mode = job["mode"].toString();
    if (mode == "images")
        return imagesPdf(job);
    auto engine = makePdfiumEngine();
    const auto opened = engine->open(job["source"].toString(), job["password"].toString());
    if (opened.error != OpenError::None)
        return fail(opened.message);
    if (mode == "search")
        return searchPdf(*engine, opened.pages, job);
    if (mode == "print")
        return printPdf(*engine, opened.pages, job);
    return fail(QStringLiteral("Neznámá operace."));
}
} // namespace airlet
