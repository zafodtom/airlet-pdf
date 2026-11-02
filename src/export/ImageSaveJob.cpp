#include "ImageSaveJob.h"
#include "i18n/Language.h"
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QSaveFile>

namespace airlet {
namespace {
QString identity(const QString &path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
bool sameFile(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return identity(a).compare(identity(b), Qt::CaseInsensitive) == 0;
#else
    return identity(a) == identity(b);
#endif
}
} // namespace
ImageSaveJob::~ImageSaveJob() {
    cancel();
    if (worker.joinable())
        worker.join();
}
void ImageSaveJob::cancel() {
    if (worker.joinable())
        worker.request_stop();
}
bool ImageSaveJob::start(QImage image, QString target, QByteArray format, QString sourcePdf,
                         bool overwrite, quint64 request) {
    if (running.exchange(true))
        return false;
    if (worker.joinable())
        worker.join();
    worker = std::jthread([this, image = std::move(image), target = std::move(target),
                           format = std::move(format), sourcePdf = std::move(sourcePdf), overwrite,
                           request](std::stop_token stop) {
        QString error;
        bool success = false;
        if (image.isNull())
            error = uiText(QStringLiteral("Výřez je prázdný."));
        else if (format != "PNG" && format != "JPG")
            error = uiText(QStringLiteral("Nepodporovaný obrázkový formát."));
        else if (QFileInfo(target).suffix().compare("pdf", Qt::CaseInsensitive) == 0 ||
                 sameFile(target, sourcePdf))
            error = uiText(QStringLiteral("Výřez nesmí přepsat PDF dokument."));
        else if (!overwrite && QFileInfo::exists(target))
            error = uiText(QStringLiteral("Cílový soubor už existuje."));
        else {
            QSaveFile file(target);
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly))
                error = file.errorString();
            else {
                QImageWriter writer(&file, format);
                if (format == "JPG")
                    writer.setQuality(92);
                if (!stop.stop_requested() && !writer.write(image))
                    error = writer.errorString();
                if (stop.stop_requested())
                    file.cancelWriting();
                else if (!error.isEmpty())
                    file.cancelWriting();
                else if ((!overwrite && QFileInfo::exists(target)) || sameFile(target, sourcePdf)) {
                    error = uiText(QStringLiteral("Cílový soubor se změnil; nic nebylo přepsáno."));
                    file.cancelWriting();
                } else if (!file.commit())
                    error = file.errorString();
                else
                    success = true;
            }
        }
        const bool cancelled = stop.stop_requested() && !success;
        running = false;
        QMetaObject::invokeMethod(
            this,
            [this, request, success, cancelled, error] {
                emit finished(request, success, cancelled, error);
            },
            Qt::QueuedConnection);
    });
    return true;
}
} // namespace airlet
