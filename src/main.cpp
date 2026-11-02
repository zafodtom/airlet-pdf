#include "export/PageExport.h"
#include "ui/MainWindow.h"
#include "ui/PdfView.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--export-worker")
            return airlet::runExportWorker(argc, argv);
    QElapsedTimer start;
    start.start();
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Airlet");
    QCoreApplication::setApplicationName("Airlet PDF");
    QCoreApplication::setApplicationVersion(AIRLET_VERSION);
    airlet::MainWindow window;
    window.show();
    const auto args = app.arguments();
    QString metricsPath;
    for (const auto &arg : args)
        if (arg.startsWith("--metrics-file="))
            metricsPath = arg.mid(15);
    const auto windowMs = start.elapsed();
    if (!metricsPath.isEmpty()) {
        QObject::connect(window.pdfView(), &airlet::PdfView::firstImage, &window,
                         [&start, windowMs, metricsPath](qint64 ms) {
                             const QJsonObject record{
                                 {"window_shown_from_main_ms", double(windowMs)},
                                 {"first_image_from_main_ms", double(start.elapsed())},
                                 {"first_image_from_open_ms", double(ms)},
                                 {"qt_version", QString::fromLatin1(qVersion())},
                                 {"build", "Release measurement requires Release binary"}};
                             QFile f(metricsPath);
                             if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                                 f.write(QJsonDocument(record).toJson());
                         });
    }
    if (args.contains("--metrics"))
        qInfo("window_shown_from_main_ms=%lld", static_cast<long long>(start.elapsed()));
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith("--")) {
            const QString path = args[i];
            QTimer::singleShot(0, &window, [&window, path] { window.openFile(path); });
            break;
        }
    }
    return app.exec();
}
