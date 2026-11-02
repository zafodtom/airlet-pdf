#include "PageExport.h"
#include "DocumentJobs.h"
#include "core/Geometry.h"
#include "core/PdfEngine.h"
#include "i18n/Language.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace airlet {
QVector<int> parsePageRange(QString range, int count, QString *error) {
    error->clear();
    QSet<int> pages;
    range.replace(QChar(0x2013), '-');
    range.replace(QChar(0x2014), '-');
    for (const auto &part : range.split(',')) {
        const auto match =
            QRegularExpression(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$")).match(part);
        bool ok = false;
        const int first = match.captured(1).toInt(&ok);
        int last = first;
        if (!match.captured(2).isEmpty())
            last = match.captured(2).toInt(&ok);
        if (!match.hasMatch() || !ok || first < 1 || last < first || last > count) {
            *error =
                uiText(QStringLiteral("Neplatný rozsah stran. Použijte například 1–5, 8, 12."));
            return {};
        }
        for (int i = first; i <= last; ++i)
            pages.insert(i - 1);
    }
    QVector<int> result(pages.begin(), pages.end());
    std::sort(result.begin(), result.end());
    return result;
}
static void send(const QJsonObject &o) {
    auto b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    b += '\n';
    std::fwrite(b.constData(), 1, b.size(), stdout);
    std::fflush(stdout);
}
int runExportWorker(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    const auto job = QJsonDocument::fromJson(input.readLine()).object();
    setLanguage(job["language"].toString());
    if (job.contains("mode"))
        return runDocumentJob(job);
    auto fail = [](QString message) {
        send({{"error", message}});
        return 1;
    };
    auto engine = makePdfiumEngine();
    const auto opened = engine->open(job["source"].toString(), job["password"].toString());
    if (opened.error != OpenError::None)
        return fail(opened.message);
    QString error;
    const auto pages = parsePageRange(job["range"].toString(), opened.pages, &error);
    if (!error.isEmpty())
        return fail(error);
    const int dpi = job["dpi"].toInt(), quality = job["quality"].toInt();
    const auto format = job["format"].toString();
    if (dpi < 72 || dpi > 600 || quality < 1 || quality > 100 ||
        (format != "PNG" && format != "JPG"))
        return fail(uiText(QStringLiteral("Neplatné parametry exportu.")));
    QDir output(job["output"].toString()), temporary(job["temporary"].toString());
    if (!output.exists() || !temporary.exists())
        return fail(uiText(QStringLiteral("Výstupní složka neexistuje.")));
    const auto base = QFileInfo(job["source"].toString()).completeBaseName();
    const int digits = std::max(3, QString::number(opened.pages).size() > 3
                                       ? int(QString::number(opened.pages).size())
                                       : 3);
    auto name = [&](int page) {
        return output.filePath(base + QStringLiteral("_%1.").arg(page + 1, digits, 10, QChar('0')) +
                               format.toLower());
    };
    for (int p : pages)
        if (QFileInfo::exists(name(p)))
            return fail(uiText(QStringLiteral("Soubor už existuje: %1. Vyberte jinou složku nebo "
                                              "přesuňte původní export."))
                            .arg(QFileInfo(name(p)).fileName()));
    const auto rotations = job["rotations"].toArray();
    int done = 0;
    for (int p : pages) {
        const int rotation = p < rotations.size() ? rotations[p].toInt() : 0;
        const auto size = rotatedSize(engine->pageInfo(p).points, rotation);
        const double w = std::ceil(size.width() * dpi / 72.),
                     h = std::ceil(size.height() * dpi / 72.);
        if (w <= 0 || h <= 0 || w > 32768 || h > 32768 || w * h > 64. * 1024 * 1024)
            return fail(uiText(QStringLiteral("Stránka %1 je při tomto DPI příliš velká. Snižte "
                                              "DPI (limit 64 megapixelů)."))
                            .arg(p + 1));
        const QSize full{int(w), int(h)};
        QImage image(full, QImage::Format_ARGB32);
        if (image.isNull())
            return fail(uiText(QStringLiteral("Nedostatek paměti.")));
        image.fill(Qt::white);
        image.setDotsPerMeterX(qRound(dpi / .0254));
        image.setDotsPerMeterY(qRound(dpi / .0254));
        for (int y = 0; y < full.height(); y += 512)
            for (int x = 0; x < full.width(); x += 512) {
                const QRect tile = QRect(x, y, 512, 512).intersected(QRect(QPoint(), full));
                auto bitmap =
                    engine->render({p, rotation, full, tile, false, {}}, [] { return false; });
                if (bitmap.isNull())
                    return fail(uiText(QStringLiteral("Stránku %1 nelze vykreslit.")).arg(p + 1));
                QPainter painter(&image);
                painter.drawImage(tile.topLeft(), bitmap);
            }
        QTemporaryFile file(temporary.filePath("page-XXXXXX.part"));
        if (!file.open())
            return fail(file.errorString());
        QImageWriter writer(&file, format.toLatin1());
        if (format == "JPG")
            writer.setQuality(quality);
        if (!writer.write(image))
            return fail(writer.errorString());
        file.close();
        // Use the owning QTemporaryFile: close() can retain its native handle.
        // Its rename closes that handle and refuses existing targets.
        if (!file.rename(name(p)))
            return fail(uiText(QStringLiteral("Výstup nelze dokončit: %1 (%2)"))
                            .arg(name(p), file.errorString()));
        file.setAutoRemove(false);
        ++done;
        send({{"completed", done}, {"total", pages.size()}, {"file", name(p)}});
    }
    send({{"success", true}});
    return 0;
}
} // namespace airlet
