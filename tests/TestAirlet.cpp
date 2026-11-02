#include "core/FolderNavigator.h"
#include "core/Geometry.h"
#include "core/PdfEngine.h"
#include "core/Selection.h"
#include "export/ImageSaveJob.h"
#include "export/PageExport.h"
#include "i18n/Language.h"
#include "render/RenderService.h"
#include "render/TileCache.h"
#include "ui/MainWindow.h"
#include "ui/PdfView.h"
#include "update/UpdateProtocol.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QWheelEvent>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <ctime>
#include <thread>

using namespace airlet;
static QString fixture(const char *name) {
    return QString::fromUtf8(FIXTURE_DIR) + "/" + name;
}
static void wheel(PdfView &view, int angle, QPoint pixels = {},
                  Qt::KeyboardModifiers modifiers = {}, Qt::ScrollPhase phase = Qt::NoScrollPhase) {
    const auto local = view.viewport()->rect().center();
    QWheelEvent event(local, view.viewport()->mapToGlobal(local), pixels, QPoint(0, angle),
                      Qt::NoButton, modifiers, phase, false);
    QApplication::sendEvent(view.viewport(), &event);
}
static void dragSelection(PdfView &view, QPointF first, QPointF last) {
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, first.toPoint());
    QTest::mouseMove(view.viewport(), last.toPoint());
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, last.toPoint());
}
struct SlowState {
    std::atomic<int> started{0}, cancelled{0};
};
class SlowEngine final : public PdfEngine {
    std::shared_ptr<SlowState> state;

  public:
    explicit SlowEngine(std::shared_ptr<SlowState> s) : state(std::move(s)) {}
    OpenResult open(const QString &, const QString &) override {
        return {OpenError::None, {}, 1, {QSizeF(300, 400)}};
    }
    void close() override {}
    PageInfo pageInfo(int) override {
        return {QSizeF(300, 400)};
    }
    TextPage text(int) override {
        return {};
    }
    QImage render(const RenderRequest &r, const std::function<bool()> &cancelled) override {
        const int job = ++state->started;
        if (job == 1) {
            // Deterministically keep an in-flight render alive until superseded.
            while (!cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++state->cancelled;
            return {};
        }
        QImage image(r.tile.size(), QImage::Format_ARGB32);
        image.fill(Qt::blue);
        return image;
    }
};
class TestAirlet final : public QObject {
    Q_OBJECT
  private slots:
    void updateMetadataValidation() {
        const QString hash(64, QChar('a'));
        const auto installer = QStringLiteral("AirletPDF-Setup-1.2.0-x64.exe");
        const auto prefix =
            QStringLiteral("https://github.com/zafodtom/airlet-pdf/releases/download/v1.2.0/");
        const QJsonObject asset{{"name", installer},
                                {"browser_download_url", prefix + installer},
                                {"size", 1024},
                                {"digest", "sha256:" + hash}};
        const QJsonObject manifestAsset{{"name", "airlet-update.json"},
                                        {"browser_download_url", prefix + "airlet-update.json"}};
        QJsonObject release{{"tag_name", "v1.2.0"},
                            {"draft", false},
                            {"prerelease", false},
                            {"assets", QJsonArray{asset, manifestAsset}}};
        UpdateRelease parsed;
        QString error;
        QCOMPARE(parseUpdateRelease(release, "1.1.0", &parsed, &error), ReleaseState::Newer);
        const QJsonObject manifest{
            {"schema", 1},
            {"version", "1.2.0"},
            {"installer", QJsonObject{{"name", installer}, {"size", 1024}, {"sha256", hash}}}};
        QVERIFY(validateUpdateManifest(manifest, &parsed, &error));
        QCOMPARE(parsed.sha256, hash);
        QCOMPARE(parseUpdateRelease(release, "1.2.0", &parsed, &error), ReleaseState::Current);
        QCOMPARE(parseUpdateRelease(release, "2.0.0", &parsed, &error), ReleaseState::Current);
        auto bad = release;
        bad["prerelease"] = true;
        QCOMPARE(parseUpdateRelease(bad, "1.1.0", &parsed, &error), ReleaseState::Invalid);
        bad = release;
        bad["tag_name"] = "v999999999999999.0.0";
        QCOMPARE(parseUpdateRelease(bad, "1.1.0", &parsed, &error), ReleaseState::Invalid);
        bad = release;
        bad["assets"] = QJsonArray{asset, manifestAsset, asset};
        QCOMPARE(parseUpdateRelease(bad, "1.1.0", &parsed, &error), ReleaseState::Invalid);
        for (const auto &url :
             {QString("http://github.com/zafodtom/airlet-pdf/releases/download/v1.2.0/") +
                  installer,
              QString("https://github.com/other/repo/releases/download/v1.2.0/") + installer,
              QString("https://github.com.evil.invalid/") + installer}) {
            auto altered = asset;
            altered["browser_download_url"] = url;
            bad = release;
            bad["assets"] = QJsonArray{altered, manifestAsset};
            QCOMPARE(parseUpdateRelease(bad, "1.1.0", &parsed, &error), ReleaseState::Invalid);
        }
        for (double size : {0., -1., .5, 1e300, 513. * 1024 * 1024}) {
            auto altered = asset;
            altered["size"] = size;
            bad = release;
            bad["assets"] = QJsonArray{altered, manifestAsset};
            QCOMPARE(parseUpdateRelease(bad, "1.1.0", &parsed, &error), ReleaseState::Invalid);
        }
        QCOMPARE(parseUpdateRelease(release, "1.1.0", &parsed, &error), ReleaseState::Newer);
        auto broken = manifest;
        auto altered = broken["installer"].toObject();
        altered["sha256"] = QString(64, QChar('b'));
        broken["installer"] = altered;
        QVERIFY(!validateUpdateManifest(broken, &parsed, &error));
        broken = manifest;
        broken["version"] = "1.3.0";
        QVERIFY(!validateUpdateManifest(broken, &parsed, &error));
        QVERIFY(
            allowedUpdateUrl(QUrl("https://release-assets.githubusercontent.com/signed/asset")));
        QVERIFY(!allowedUpdateUrl(QUrl("file:///tmp/setup.exe")));
        QVERIFY(!allowedUpdateUrl(QUrl("https://name:password@github.com/asset")));
        QVERIFY(!allowedUpdateUrl(QUrl("https://github.com:8080/asset")));
#ifdef Q_OS_WIN
        QTemporaryDir dir;
        QFile file(dir.filePath(QString::fromUtf8("ověření & data.bin")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("airlet verifier test");
        file.close();
        const auto sha = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray("airlet verifier test"), QCryptographicHash::Sha256)
                .toHex());
        auto verify = [&](QString hash) {
            QProcess process;
            process.start(QString::fromUtf8(APPLY_EXE), {"--verify", file.fileName(), hash});
            if (!process.waitForFinished(10000))
                return -1;
            return process.exitCode();
        };
        QCOMPARE(verify(sha), 0);
        QCOMPARE(verify(QString(64, QChar('0'))), 1);
#endif
    }
    void backwardFolderContinuation() {
        QTemporaryDir dir;
        const auto one = dir.filePath("sheet1.pdf"), two = dir.filePath("sheet2.pdf");
        QVERIFY(QFile::copy(fixture("mixed-pages.pdf"), one));
        QVERIFY(QFile::copy(fixture("scan.pdf"), two));
        FolderNavigator nav;
        QSignalSpy result(&nav, &FolderNavigator::resolved);
        nav.request(two, -1);
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(result[0][2].toString(), one);
        MainWindow window;
        window.show();
        auto *view = window.pdfView();
        auto *toggle = window.findChild<QAction *>("continueDocuments");
        QVERIFY(toggle);
        for (auto *bar : window.findChildren<QToolBar *>())
            QVERIFY(bar->actions().contains(toggle));
        bool settings = false, language = false;
        for (auto *a : window.findChildren<QAction *>()) {
            settings |= a->text() == QString::fromUtf8("Nastavení");
            language |= a->text() == QString::fromUtf8("Jazyk");
            QVERIFY(a->text() != "Jazyk / Language");
        }
        QVERIFY(settings);
        QVERIFY(language);
        QSignalSpy opened(view, &PdfView::documentOpened), first(view, &PdfView::firstImage);
        window.openFile(two);
        QTRY_COMPARE(first.size(), 1);
        toggle->setChecked(true);
        wheel(*view, 120);
        QTRY_COMPARE(opened.size(), 2);
        QCOMPARE(opened[1][0].toString(), one);
        QTRY_COMPARE(first.size(), 2);
        QTRY_COMPARE(view->currentPage(), 2);
        QTRY_COMPARE(view->verticalScrollBar()->value(), view->verticalScrollBar()->maximum());
        for (int i = 0; i < 5; ++i)
            wheel(*view, 120);
        QTest::qWait(30);
        QCOMPARE(opened.size(), 2);
        QTest::qWait(650);
        view->goToPage(0);
        QTest::qWait(30);
        wheel(*view, 120);
        QTRY_VERIFY(window.statusBar()->currentMessage().contains(QString::fromUtf8("předchozí")));
        QCOMPARE(opened.size(), 2);
        // Reaching the upper edge is distinct from moving beyond it.
        toggle->setChecked(false);
        toggle->setChecked(true);
        window.openFile(two);
        QTRY_COMPARE(first.size(), 3);
        view->setZoom(3.);
        view->verticalScrollBar()->setValue(1);
        wheel(*view, 120);
        QCOMPARE(opened.size(), 3);
        wheel(*view, 120);
        QTRY_COMPARE(opened.size(), 4);
        QCOMPARE(opened[3][0].toString(), one);
    }
    void folderOrderAndContinuation() {
        QVERIFY(compareDocumentNames("sheet2.pdf", "sheet10.pdf") < 0);
        QVERIFY(compareDocumentNames("sheet10.pdf", "sheet100.pdf") < 0);
        QVERIFY(compareDocumentNames("sheet2.pdf", "sheet02.pdf") != 0);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto one = dir.filePath("sheet1.pdf"), two = dir.filePath("sheet2.PDF"),
                   ten = dir.filePath("sheet10.pdf");
        QVERIFY(QFile::copy(fixture("scan.pdf"), one));
        QVERIFY(QFile::copy(fixture("scan.pdf"), two));
        QVERIFY(QFile::copy(fixture("basic.pdf"), ten));
        QVERIFY(QDir(dir.path()).mkdir("nested"));
        QVERIFY(QFile::copy(fixture("scan.pdf"), dir.filePath("nested/sheet3.pdf")));
        FolderNavigator nav;
        QSignalSpy resolved(&nav, &FolderNavigator::resolved);
        const auto id = nav.request(one);
        QTRY_COMPARE(resolved.size(), 1);
        QCOMPARE(resolved[0][0].toULongLong(), id);
        QCOMPARE(resolved[0][2].toString(), two);
        MainWindow window;
        window.show();
        auto *view = window.pdfView();
        QSignalSpy opened(view, &PdfView::documentOpened), first(view, &PdfView::firstImage);
        window.openFile(one);
        QTRY_COMPARE(first.size(), 1);
        auto *toggle = window.findChild<QAction *>("continueDocuments");
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        view->fitPage();
        wheel(*view, -120);
        QTest::qWait(30);
        QCOMPARE(opened.size(), 1);
        toggle->setChecked(true);
        wheel(*view, -120);
        QTRY_COMPARE(opened.size(), 2);
        QCOMPARE(opened[1][0].toString(), two);
        QTRY_COMPARE(first.size(), 2);
        for (int i = 0; i < 8; ++i)
            wheel(*view, -120);
        QTest::qWait(30);
        QCOMPARE(opened.size(), 2);
        QTest::qWait(650);
        wheel(*view, -120);
        QTRY_COMPARE(opened.size(), 3);
        QCOMPARE(opened[2][0].toString(), ten);
        QTRY_COMPARE(first.size(), 3);
        view->goToPage(2);
        QTest::qWait(30);
        QCOMPARE(opened.size(), 3);
        QTest::qWait(650);
        wheel(*view, -120);
        QTRY_VERIFY(window.statusBar()->currentMessage().contains(QString::fromUtf8("není další")));
        QCOMPARE(opened.size(), 3); // no wrap at folder end
        MainWindow fresh;
        QVERIFY(!fresh.findChild<QAction *>("continueDocuments")->isChecked());

        // Switching off before a queued directory result prevents a late transition.
        window.openFile(one);
        QTRY_COMPARE(first.size(), 4);
        view->fitPage();
        QTest::qWait(650);
        wheel(*view, -120);
        toggle->setChecked(false);
        QTest::qWait(150);
        QCOMPARE(opened.size(), 4);
    }
    void continuationAtScrollBoundary() {
        PdfView view;
        view.resize(600, 500);
        view.show();
        QSignalSpy first(&view, &PdfView::firstImage), next(&view, &PdfView::nextDocumentRequested);
        view.openFile(fixture("scan.pdf"));
        QTRY_COMPARE(first.size(), 1);
        view.setContinueToNextDocument(true);
        view.setZoom(3.);
        QVERIFY(view.verticalScrollBar()->maximum() > 0);
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() - 1);
        wheel(view, -120);
        QCOMPARE(next.size(), 0); // merely reaching the end
        wheel(view, -120);
        QCOMPARE(next.size(), 1);
        view.setContinueToNextDocument(false);
        view.setContinueToNextDocument(true);
        wheel(view, 0, QPoint(0, -120), Qt::NoModifier, Qt::ScrollMomentum);
        QCOMPARE(next.size(), 1);
        wheel(view, -120, {}, Qt::ControlModifier);
        QCOMPARE(next.size(), 1);
        wheel(view, -120, {}, Qt::ShiftModifier);
        QCOMPARE(next.size(), 1);
        view.fitPage();
        wheel(view, 0, QPoint(0, -200), Qt::NoModifier, Qt::ScrollMomentum);
        QCOMPARE(next.size(), 1);
        wheel(view, -120);
        QCOMPARE(next.size(), 2);
        view.setContinueToNextDocument(false);
        wheel(view, -120);
        QCOMPARE(next.size(), 2);
    }
    void englishInterfaceAndWorker() {
        struct Restore {
            ~Restore() {
                setLanguage("cs");
            }
        } restore;
        setLanguage("en");
        QCOMPARE(uiText(QString::fromUtf8("Otevřít…")), QString::fromUtf8("Open…"));
        QCOMPARE(uiText(QString::fromUtf8("Pokračovat dalším PDF")),
                 QString("Continue to next PDF"));
        QCOMPARE(languageCode(), QString("en"));
        {
            MainWindow window;
            bool open = false, help = false, settings = false, language = false;
            for (auto *a : window.findChildren<QAction *>()) {
                open |= a->text() == QString::fromUtf8("Open…");
                help |= a->text() == "Help";
                settings |= a->text() == "Settings";
                language |= a->text() == "Language";
            }
            QVERIFY(open);
            QVERIFY(help);
            QVERIFY(settings);
            QVERIFY(language);
        }
        QProcess worker;
        worker.start(QString::fromUtf8(EXPORT_EXE), {});
        QVERIFY(worker.waitForStarted(5000));
        worker.write(QJsonDocument(QJsonObject{{"language", "en"},
                                               {"mode", "search"},
                                               {"source", fixture("basic.pdf")},
                                               {"query", ""}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        worker.closeWriteChannel();
        QVERIFY(worker.waitForFinished(10000));
        QCOMPARE(worker.exitCode(), 1);
        QVERIFY(worker.readAllStandardOutput().contains("Enter no more than 256 characters."));
    }
    void continuousHighlightBands() {
        // Varying ascenders/descenders, kerning overlap and a word space form one band.
        const QVector<QRectF> boxes{{10, 10, 6, 12}, {15, 12, 5, 7},  {23, 11, 7, 9},
                                    {34, 12, 5, 7},  {10, 30, 5, 10}, {17, 32, 5, 6},
                                    {100, 30, 5, 10}};
        const auto bands = highlightBands(boxes);
        QCOMPARE(bands.size(), 3);
        QCOMPARE(bands[0], QRectF(10, 10, 29, 12));
        QCOMPARE(bands[1], QRectF(10, 30, 12, 10));
        QCOMPARE(bands[2], QRectF(100, 30, 5, 10));
        const auto vertical = highlightBands({{10, 10, 12, 6}, {12, 17, 7, 5}, {11, 24, 9, 7}});
        QCOMPARE(vertical, QVector<QRectF>({QRectF(10, 10, 12, 21)}));
        QCOMPARE(highlightBands({{10, 10, 5, 10}, QRectF(), {10, 21, 5, 10}}).size(), 2);
        QVERIFY(highlightBands({}).isEmpty());
        for (int rotation = 0; rotation < 4; ++rotation) {
            const auto rect = rotateRect(bands.first(), QSizeF(300, 400), rotation);
            QVERIFY(rect.isValid());
        }
        MainWindow window;
        QVERIFY(!window.windowIcon().isNull());
        QVERIFY(!window.windowIcon().pixmap(32, 32).isNull());
        for (auto *bar : window.findChildren<QToolBar *>())
            for (auto *a : bar->actions())
                QVERIFY(a->text() != QString::fromUtf8("Otevřít…") &&
                        a->text() != QString::fromUtf8("Zavřít") &&
                        a->text() != QString::fromUtf8("Export…") &&
                        a->text() != QString::fromUtf8("O aplikaci"));
    }
    void documentJobsIntegration() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto run = [&](QJsonObject job, QJsonArray *records = nullptr) {
            QProcess worker;
            worker.start(QString::fromUtf8(EXPORT_EXE), {});
            if (!worker.waitForStarted(5000))
                return -999;
            worker.write(QJsonDocument(job).toJson(QJsonDocument::Compact) + '\n');
            worker.closeWriteChannel();
            if (!worker.waitForFinished(20000)) {
                worker.kill();
                worker.waitForFinished();
                return -998;
            }
            const auto bytes = worker.readAllStandardOutput();
            if (records)
                for (const auto &line : bytes.split('\n'))
                    if (!line.isEmpty())
                        records->append(QJsonDocument::fromJson(line).object());
            if (worker.exitCode())
                qInfo().noquote() << bytes << worker.readAllStandardError();
            return worker.exitStatus() == QProcess::NormalExit ? worker.exitCode() : -997;
        };
        QJsonObject search{
            {"mode", "search"}, {"source", fixture("basic.pdf")}, {"query", "aIRLET TeST"}};
        QJsonArray records;
        QCOMPARE(run(search, &records), 0);
        int matches = 0;
        for (const auto &v : records)
            for (const auto &m : v.toObject()["matches"].toArray()) {
                ++matches;
                QVERIFY(!m.toObject()["rects"].toArray().isEmpty());
            }
        QCOMPARE(matches, 3);
        search["source"] = fixture("czech-text.pdf");
        search["query"] = QString::fromUtf8("KŮŇ ÚPĚL");
        records = {};
        QCOMPARE(run(search, &records), 0);
        QCOMPARE(records[0].toObject()["matches"].toArray().size(), 1);
        search["source"] = fixture("scan.pdf");
        records = {};
        QCOMPARE(run(search, &records), 0);
        QCOMPARE(records.last().toObject()["textPages"].toInt(), 0);
        search["source"] = fixture("encrypted.pdf");
        QCOMPARE(run(search), 1);
        search["password"] = "airlet-test";
        search["query"] = "Airlet";
        QCOMPARE(run(search), 0);

        QImage red(120, 60, QImage::Format_RGB32);
        red.fill(Qt::red);
        QImage blue(60, 120, QImage::Format_RGB32);
        blue.fill(Qt::blue);
        const auto redPath = dir.filePath(QString::fromUtf8("červená.png")),
                   bluePath = dir.filePath("blue.jpg");
        QVERIFY(red.save(redPath));
        QVERIFY(blue.save(bluePath));
        const auto target = dir.filePath(QString::fromUtf8("obrázky.pdf"));
        QJsonObject images{{"mode", "images"}, {"images", QJsonArray{bluePath, redPath}},
                           {"output", target}, {"temporary", dir.path()},
                           {"dpi", 72},        {"paper", 0}};
        QCOMPARE(run(images), 0);
        auto engine = makePdfiumEngine();
        auto opened = engine->open(target, {});
        QCOMPARE(opened.error, OpenError::None);
        QCOMPARE(opened.pages, 2);
        QCOMPARE(engine->pageInfo(0).points, QSizeF(60, 120));
        QCOMPARE(engine->pageInfo(1).points, QSizeF(120, 60));
        auto first = engine->render({0, 0, QSize(60, 120), QRect(0, 0, 60, 120), false, {}},
                                    [] { return false; });
        QVERIFY(first.pixelColor(30, 60).blue() > 240);
        QVERIFY(first.pixelColor(30, 60).red() < 20);
        auto second = engine->render({1, 0, QSize(120, 60), QRect(0, 0, 120, 60), false, {}},
                                     [] { return false; });
        QVERIFY(second.pixelColor(60, 30).red() > 240);
        engine->close();
        QFile existing(target);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        const auto original = existing.readAll();
        existing.close();
        QCOMPARE(run(images), 1);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), original);
        existing.close();
        images["output"] = dir.filePath("a4.pdf");
        images["paper"] = 2;
        QCOMPARE(run(images), 0);
        QCOMPARE(engine->open(images["output"].toString(), {}).error, OpenError::None);
        QVERIFY(std::abs(engine->pageInfo(0).points.width() - 841.89) < 2);
        engine->close();
        images["output"] = dir.filePath("broken.pdf");
        images["images"] = QJsonArray{redPath, dir.filePath("missing.png")};
        QCOMPARE(run(images), 1);
        QVERIFY(!QFile::exists(images["output"].toString()));

        QJsonObject print{{"mode", "print"},
                          {"source", fixture("basic.pdf")},
                          {"range", "1,3"},
                          {"rotations", QJsonArray{1, 0, 0}},
                          {"paperMm", QJsonArray{297, 210}},
                          {"pdf", true},
                          {"temporary", dir.path()},
                          {"output", dir.filePath("printed.pdf")},
                          {"resolution", 150},
                          {"copies", 1}};
        QCOMPARE(run(print), 0);
        opened = engine->open(print["output"].toString(), {});
        QCOMPARE(opened.error, OpenError::None);
        QCOMPARE(opened.pages, 2);
        QVERIFY(engine->pageInfo(0).points.width() > engine->pageInfo(0).points.height());
        engine->close();
        QCOMPARE(run(print), 1);
        print["output"] = dir.filePath("invalid-print.pdf");
        print["range"] = "0";
        QCOMPARE(run(print), 1);
        QVERIFY(!QFile::exists(print["output"].toString()));

        // No partially built PDF may appear at the target when conversion is killed.
        QJsonArray many;
        for (int i = 0; i < 1000; ++i)
            many.append(redPath);
        images["images"] = many;
        images["output"] = dir.filePath("cancelled.pdf");
        QProcess cancelled;
        cancelled.start(QString::fromUtf8(EXPORT_EXE), {});
        QVERIFY(cancelled.waitForStarted(5000));
        cancelled.write(QJsonDocument(images).toJson(QJsonDocument::Compact) + '\n');
        cancelled.closeWriteChannel();
        QVERIFY(cancelled.waitForReadyRead(10000));
        cancelled.kill();
        QVERIFY(cancelled.waitForFinished(5000));
        QVERIFY(!QFile::exists(images["output"].toString()));
    }
    void searchWindowLifecycle() {
        MainWindow window;
        window.show();
        window.openFile(fixture("basic.pdf"));
        QTRY_COMPARE(window.pdfView()->pageCount(), 3);
        QAction *find = nullptr;
        for (auto *a : window.findChildren<QAction *>())
            if (a->shortcut() == QKeySequence::Find)
                find = a;
        QVERIFY(find);
        find->trigger();
        auto *query = window.findChild<QLineEdit *>("searchQuery");
        auto *status = window.findChild<QLabel *>("searchStatus");
        QVERIFY(query);
        QVERIFY(status);
        query->setText("Airlet test");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("1 / 3"), 10000);
        window.pdfView()->rotate(1);
        window.pdfView()->setZoom(1.5);
        QTest::keyClick(query, Qt::Key_Return);
        QVERIFY(status->text().startsWith("2 / 3"));
        window.openFile(fixture("mixed-pages.pdf"));
        QTRY_COMPARE(window.pdfView()->pageCount(), 3);
        const QRectF hit(20, 800, 30, 20);
        window.pdfView()->showSearchHit(2, {hit}, QSizeF(200, 1000));
        const auto visible = window.pdfView()->pageToViewport(2, hit.center());
        QVERIFY(visible.y() >= 0 && visible.y() <= window.pdfView()->viewport()->height());
        window.openFile(fixture("scan.pdf"));
        QTRY_COMPARE(window.pdfView()->pageCount(), 1);
        auto *dialog = window.findChild<QDialog *>("searchDialog");
        QVERIFY(dialog);
        QVERIFY(!dialog->isVisible());
        find->trigger();
        query->setText("nenalezeno");
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("bez OCR")), 10000);
        dialog->reject();
        QTest::qWait(30);
        const auto jobs = window.pdfView()->jobsStarted();
        QTest::qWait(100);
        QCOMPARE(window.pdfView()->jobsStarted(), jobs);
    }
    void exportWorkerIntegration() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        root.mkdir("parts");
        QJsonObject job{{"source", fixture("basic.pdf")},
                        {"password", ""},
                        {"range", "1, 3"},
                        {"dpi", 72},
                        {"quality", 85},
                        {"format", "PNG"},
                        {"output", dir.path()},
                        {"temporary", root.filePath("parts")},
                        {"rotations", QJsonArray{1, 0, 0}}};
        auto run = [&](QJsonObject data) {
            QProcess process;
            process.start(QString::fromUtf8(EXPORT_EXE), {"--export-worker"});
            if (!process.waitForStarted(5000))
                return -999;
            process.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n");
            process.closeWriteChannel();
            if (!process.waitForFinished(15000)) {
                process.kill();
                process.waitForFinished();
                return -998;
            }
            if (process.exitCode() != 0 || process.exitStatus() != QProcess::NormalExit) {
                qWarning().noquote() << "Export worker status:" << process.exitStatus()
                                     << "exit code:" << process.exitCode()
                                     << "stdout:" << process.readAllStandardOutput()
                                     << "stderr:" << process.readAllStandardError();
            }
            return process.exitCode();
        };
        QCOMPARE(run(job), 0);
        QCOMPARE(QImage(root.filePath("basic_001.png")).size(), QSize(400, 300));
        QCOMPARE(QImage(root.filePath("basic_003.png")).size(), QSize(300, 400));
        QVERIFY(!QFile::exists(root.filePath("basic_002.png")));
        QFile file(root.filePath("basic_001.png"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        file.close();
        QCOMPARE(run(job), 1);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        file.close();
        job["format"] = "JPG";
        job["range"] = "2";
        QCOMPARE(run(job), 0);
        QCOMPARE(QImage(root.filePath("basic_002.jpg")).size(), QSize(300, 400));
        job["range"] = "0";
        QCOMPARE(run(job), 1);
        job["source"] = fixture("encrypted.pdf");
        job["password"] = "airlet-test";
        job["range"] = "1";
        QCOMPARE(run(job), 0);
        QVERIFY(QFile::exists(root.filePath("encrypted_001.jpg")));
        job["source"] = fixture("long.pdf");
        job["password"] = "";
        job["range"] = "1-500";
        job["dpi"] = 300;
        QProcess cancelled;
        cancelled.start(QString::fromUtf8(EXPORT_EXE), {"--export-worker"});
        QVERIFY(cancelled.waitForStarted(5000));
        cancelled.write(QJsonDocument(job).toJson(QJsonDocument::Compact) + "\n");
        cancelled.closeWriteChannel();
        QVERIFY(cancelled.waitForReadyRead(10000));
        cancelled.kill();
        QVERIFY(cancelled.waitForFinished(5000));
        for (const auto &name : root.entryList({"long_*.jpg"}, QDir::Files))
            QVERIFY(!QImage(root.filePath(name)).isNull());
        QVERIFY(QDir(root.filePath("parts")).removeRecursively());
    }
    void exportRanges() {
        QString error;
        QCOMPARE(parsePageRange(QString::fromUtf8("1–3, 8, 3"), 10, &error),
                 QVector<int>({0, 1, 2, 7}));
        QVERIFY(error.isEmpty());
        for (const auto &range : {"0", "3-1", "1,", "11", "1-a", ""}) {
            QVERIFY(parsePageRange(range, 10, &error).isEmpty());
            QVERIFY(!error.isEmpty());
        }
    }
    void initTestCase() {
        qRegisterMetaType<OpenResult>();
        qRegisterMetaType<RenderRequest>();
        qRegisterMetaType<PageInfo>();
        qRegisterMetaType<TextPage>();
    }
    void coordinates_data() {
        QTest::addColumn<int>("rotation");
        for (int r = -4; r <= 7; ++r)
            QTest::newRow(qPrintable(QString::number(r))) << r;
    }
    void coordinates() {
        QFETCH(int, rotation);
        const QSizeF size(300, 700);
        for (const auto p : {QPointF(0, 0), QPointF(300, 700), QPointF(21.7, 413.9)}) {
            const auto actual = unrotatePoint(rotatePoint(p, size, rotation), size, rotation);
            QVERIFY(QLineF(p, actual).length() < 1e-8);
        }
    }
    void openUnicodeAndPassword() {
        auto e = makePdfiumEngine();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto p = dir.filePath(QString::fromUtf8("příručka-日本.pdf"));
        QVERIFY(QFile::copy(fixture("basic.pdf"), p));
        QCOMPARE(e->open(p, {}).error, OpenError::None);
        QCOMPARE(e->open(fixture("encrypted.pdf"), {}).error, OpenError::Password);
        QCOMPARE(e->open(fixture("encrypted.pdf"), "wrong").error, OpenError::Password);
        QCOMPARE(e->open(fixture("encrypted.pdf"), "airlet-test").error, OpenError::None);
        QCOMPARE(e->open(dir.filePath("absent.pdf"), {}).error, OpenError::Io);
    }
    void tileMatchesFull_data() {
        QTest::addColumn<int>("rotation");
        for (int r = 0; r < 4; ++r)
            QTest::newRow(qPrintable(QString::number(r))) << r;
    }
    void tileMatchesFull() {
        QFETCH(int, rotation);
        auto e = makePdfiumEngine();
        QCOMPARE(e->open(fixture("basic.pdf"), {}).error, OpenError::None);
        const auto size = rotatedSize(e->pageInfo(0).points, rotation).toSize();
        RenderRequest request{0, rotation, size, QRect(QPoint(), size), false, {}};
        const auto full = e->render(request, [] { return false; });
        QVERIFY(!full.isNull());
        const QRect rect(47, 63, 90, 100);
        request.tile = rect;
        const auto tile = e->render(request, [] { return false; });
        QVERIFY(!tile.isNull());
        QCOMPARE(tile, full.copy(rect));
        QVERIFY(full.pixelColor(10, 10) != full.pixelColor(full.width() / 2, full.height() / 2));
    }
    void cancelAndBounds() {
        auto e = makePdfiumEngine();
        e->open(fixture("basic.pdf"), {});
        RenderRequest r{0, 0, QSize(300, 400), QRect(0, 0, 300, 400), false, {}};
        QVERIFY(e->render(r, [] { return true; }).isNull());
        r.tile = QRect(-1, 0, 300, 400);
        QVERIFY(e->render(r, [] { return false; }).isNull());
        r.tile = QRect(0, 0, 3000, 3000);
        QVERIFY(e->render(r, [] { return false; }).isNull());
    }
    void cropIntrinsicRotationAndText() {
        auto e = makePdfiumEngine();
        QCOMPARE(e->open(fixture("cropped-rotated.pdf"), {}).error, OpenError::None);
        QCOMPARE(e->pageInfo(0).points, QSizeF(240, 160));
        const auto t = e->text(0);
        QVERIFY(t.copyAllowed);
        QVERIFY(!t.glyphs.isEmpty());
        for (const auto &glyph : t.glyphs)
            QVERIFY(QRectF(0, 0, 240, 160).adjusted(-2, -2, 2, 2).contains(glyph.bounds));
    }
    void scannedHasNoText() {
        auto e = makePdfiumEngine();
        QCOMPARE(e->open(fixture("scan.pdf"), {}).error, OpenError::None);
        QVERIFY(e->text(0).glyphs.isEmpty());
    }
    void cacheBound() {
        TileCache cache(2);
        QImage image(512, 512, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        for (int i = 0; i < 500; ++i)
            cache.put(QString::number(i), image.copy());
        QVERIFY(cache.bytes() <= 2 * 1024 * 1024);
        QVERIFY(cache.contains("499"));
        QVERIFY(!cache.contains("0"));
        cache.clear();
        QCOMPARE(cache.bytes(), 0);
    }
    void serviceReplacementAndIdle() {
        RenderService service;
        QSignalSpy opened(&service, &RenderService::opened),
            rendered(&service, &RenderService::rendered);
        service.open(1, fixture("long.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
        auto result = qvariant_cast<OpenResult>(opened[0][1]);
        QCOMPARE(result.pages, 500);
        RenderRequest r{0, 0, QSize(300, 400), QRect(0, 0, 300, 400), false, "old"};
        service.submit(1, {r});
        service.open(2, fixture("basic.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 5000);
        r.key = "new";
        const auto gen = service.submit(2, {r});
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(service.jobsFinished() == service.jobsStarted(), 5000);
        bool sawNew = false;
        for (const auto &row : rendered)
            if (row[0].toULongLong() == 2 && row[1].toULongLong() == gen)
                sawNew = true;
        QVERIFY(sawNew);
        const auto jobs = service.jobsStarted();
        QTest::qWait(300);
        QCOMPARE(service.jobsStarted(), jobs);
        service.close(3);
        QTest::qWait(50);
    }
    void cancelInFlightAndJoin() {
        auto state = std::make_shared<SlowState>();
        auto service = std::make_unique<RenderService>(
            nullptr, [state] { return std::make_unique<SlowEngine>(state); });
        QSignalSpy opened(service.get(), &RenderService::opened),
            rendered(service.get(), &RenderService::rendered);
        service->open(1, "first");
        QTRY_COMPARE(opened.size(), 1);
        RenderRequest r{0, 0, QSize(300, 400), QRect(0, 0, 300, 400), false, "first"};
        service->submit(1, {r});
        QTRY_COMPARE(state->started.load(), 1);
        service->open(2, "second");
        QTRY_COMPARE(opened.size(), 2);
        QCOMPARE(state->cancelled.load(), 1);
        r.key = "second";
        service->submit(2, {r});
        QTRY_COMPARE(rendered.size(), 1);
        QCOMPARE(rendered[0][0].toULongLong(), quint64(2));
        QCOMPARE(qvariant_cast<QImage>(rendered[0][4]).pixelColor(0, 0), QColor(Qt::blue));
        service.reset();
        state = std::make_shared<SlowState>();
        service = std::make_unique<RenderService>(
            nullptr, [state] { return std::make_unique<SlowEngine>(state); });
        QSignalSpy openedAgain(service.get(), &RenderService::opened);
        service->open(3, "third");
        QTRY_COMPARE(openedAgain.size(), 1);
        service->submit(3, {r});
        QTRY_COMPARE(state->started.load(), 1);
        QElapsedTimer timer;
        timer.start();
        service.reset();
        QCOMPARE(state->cancelled.load(), 1);
        QVERIFY(timer.elapsed() < 2000);
    }
    void viewZoomScrollCloseAndIdle() {
        PdfView view;
        view.resize(900, 650);
        view.show();
        QSignalSpy first(&view, &PdfView::firstImage);
        view.openFile(fixture("long.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(view.pageCount(), 500, 5000);
        for (int i = 0; i < 20; ++i) {
            view.goToPage(i * 17);
            view.setZoom(.4 + (i % 5) * .3, QPoint(250, 200));
        }
        first.clear();
        view.openFile(fixture("basic.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(view.pageCount(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!first.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(view.jobsStarted(), view.jobsFinished(), 5000);
        const auto count = view.jobsStarted();
        QEventLoop idle;
        const auto cpu = std::clock();
        QTimer::singleShot(500, &idle, &QEventLoop::quit);
        idle.exec();
        QCOMPARE(view.jobsStarted(), count);
        qInfo("idle_process_cpu_ms_over_500ms=%.3f", 1000. * (std::clock() - cpu) / CLOCKS_PER_SEC);
        QVERIFY(view.cachedBytes() <= 112LL * 1024 * 1024);
        const auto previous = view.jobsStarted();
        view.rotate(1);
        view.setZoom(2., QPoint(300, 200));
        view.goToPage(1);
        QTRY_VERIFY_WITH_TIMEOUT(view.jobsStarted() > previous, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(view.jobsStarted(), view.jobsFinished(), 5000);
        QTest::qWait(50);
        const auto output = qEnvironmentVariable("AIRLET_TEST_OUTPUT");
        if (!output.isEmpty())
            QVERIFY(view.grab().save(output + "/viewer.png"));
        view.closeDocument();
        QCOMPARE(view.pageCount(), 0);
        QTest::qWait(50);
        QCOMPARE(view.cachedBytes(), 0);
    }
    void mainWindowSmoke() {
        MainWindow window;
        window.resize(1050, 800);
        window.show();
        QSignalSpy first(window.pdfView(), &PdfView::firstImage);
        window.openFile(fixture("basic.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(!first.isEmpty(), 5000);
        QCOMPARE(window.pdfView()->pageCount(), 3);
        QCOMPARE(window.pdfView()->verticalScrollBar()->value(), 0);
        const auto output = qEnvironmentVariable("AIRLET_TEST_OUTPUT");
        if (!output.isEmpty())
            QVERIFY(window.grab().save(output + "/mainwindow.png"));
        window.close();
    }
    void defaultPageViewAndSharedToolbarState() {
        MainWindow window;
        window.resize(1050, 800);
        window.show();
        auto *view = window.pdfView();
        auto *pageMode = window.findChild<QAction *>("fitPage");
        auto *widthMode = window.findChild<QAction *>("fitWidth");
        auto *actualSize = window.findChild<QAction *>("actualSize");
        auto *continuation = window.findChild<QAction *>("continueDocuments");
        QVERIFY(pageMode && widthMode && actualSize && continuation);
        auto *bar = window.findChild<QToolBar *>();
        QVERIFY(bar && bar->actions().contains(continuation));
        bool inMenu = false;
        for (auto *menu : window.findChildren<QMenu *>())
            inMenu |= menu->actions().contains(continuation);
        QVERIFY(inMenu);
        QVERIFY(!continuation->isChecked());
        QSignalSpy opened(view, &PdfView::documentOpened);
        window.openFile(fixture("basic.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
        QVERIFY(pageMode->isChecked());
        QVERIFY(!widthMode->isChecked());
        auto insidePage = [&] {
            const auto top = view->pageToViewport(0, {0, 0});
            const auto bottom = view->pageToViewport(0, {300, 400});
            return top.x() >= -1 && top.y() >= -1 && bottom.x() <= view->viewport()->width() + 1 &&
                   bottom.y() <= view->viewport()->height() + 1;
        };
        QTRY_VERIFY(insidePage());
        widthMode->trigger();
        QVERIFY(widthMode->isChecked());
        QVERIFY(!pageMode->isChecked());
        view->fitPage(); // Includes changes made outside toolbar actions.
        QVERIFY(pageMode->isChecked());
        view->setZoom(1.);
        QVERIFY(actualSize->isChecked());
        view->setZoom(1.5);
        QVERIFY(!actualSize->isChecked());
        QVERIFY(!widthMode->isChecked());
        QVERIFY(!pageMode->isChecked());
        window.openFile(fixture("basic.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 5000);
        QTRY_VERIFY(insidePage());
        QVERIFY(pageMode->isChecked());
        window.close();
    }
    void landscapeFitDoesNotOscillate() {
        MainWindow window;
        window.resize(1050, 800);
        window.show();
        QSignalSpy first(window.pdfView(), &PdfView::firstImage);
        window.openFile(fixture("technical-a0.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(first.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(window.pdfView()->jobsStarted(), window.pdfView()->jobsFinished(),
                                  5000);
        const auto jobs = window.pdfView()->jobsStarted();
        QEventLoop idle;
        QTimer::singleShot(200, &idle, &QEventLoop::quit);
        idle.exec();
        QCOMPARE(window.pdfView()->jobsStarted(), jobs);
        window.close();
    }
    void wheelPagesAndContinuousScroll() {
        PdfView view;
        view.resize(900, 650);
        view.show();
        view.openFile(fixture("basic.pdf"));
        QTRY_COMPARE(view.pageCount(), 3);
        view.fitPage();
        view.goToPage(0);
        wheel(view, -60);
        QCOMPARE(view.currentPage(), 0);
        wheel(view, -60);
        QCOMPARE(view.currentPage(), 1);
        wheel(view, -120);
        QCOMPARE(view.currentPage(), 2);
        wheel(view, -120);
        QCOMPARE(view.currentPage(), 2);
        wheel(view, 240);
        QCOMPARE(view.currentPage(), 0);
        const auto before = view.zoom();
        wheel(view, 120, {}, Qt::ControlModifier);
        QVERIFY(view.zoom() > before);
        view.fitWidth();
        view.goToPage(0);
        const int previousLines = QApplication::wheelScrollLines();
        QApplication::setWheelScrollLines(3);
        wheel(view, -120);
        QVERIFY(view.verticalScrollBar()->value() >= 90);
        view.goToPage(0);
        wheel(view, 0, QPoint(0, -27));
        QCOMPARE(view.verticalScrollBar()->value(), 27);
        QApplication::setWheelScrollLines(previousLines);
    }
    void wholePageWheelFitsMixedSizes() {
        PdfView view;
        view.resize(900, 650);
        view.show();
        view.openFile(fixture("mixed-pages.pdf"));
        QTRY_COMPARE(view.pageCount(), 3);
        view.fitPage();
        wheel(view, -120);
        QTRY_COMPARE(view.currentPage(), 1);
        const auto expected = std::min((view.viewport()->width() - 32.) / (600. * 96. / 72.),
                                       (view.viewport()->height() - 32.) / (300. * 96. / 72.));
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(view.zoom() - expected) < .001, 5000);
        wheel(view, -120);
        QTRY_COMPARE(view.currentPage(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(
            std::abs(view.zoom() -
                     std::min((view.viewport()->width() - 32.) / (200. * 96. / 72.),
                              (view.viewport()->height() - 32.) / (1000. * 96. / 72.))) < .001,
            5000);
    }
    void czechMultilineText() {
        auto engine = makePdfiumEngine();
        QCOMPARE(engine->open(fixture("czech-text.pdf"), {}).error, OpenError::None);
        const auto text = engine->text(0);
        QVERIFY(!text.glyphs.isEmpty());
        QCOMPARE(airlet::selectedText(text, 0, text.glyphs.size() - 1),
                 QString::fromUtf8("Příliš žluťoučký kůň\nÚpěl ďábelské ódy."));
    }
    void mainWindowCropClipboard() {
        MainWindow window;
        window.resize(1050, 800);
        window.show();
        window.openFile(fixture("basic.pdf"));
        auto &view = *window.pdfView();
        QTRY_COMPARE(view.pageCount(), 3);
        view.setZoom(.75);
        view.goToPage(0);
        view.setSelectionMode(PdfView::SelectionMode::Area);
        dragSelection(view, view.pageToViewport(0, QPointF(80, 100)),
                      view.pageToViewport(0, QPointF(150, 170)));
        QApplication::clipboard()->clear();
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!QApplication::clipboard()->image().isNull(), 5000);
        QCOMPARE(QApplication::clipboard()->image().size(), QSize(140, 140));
        window.close();
    }
    void selectionGeometryAndText_data() {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<double>("zoom");
        QTest::addColumn<QString>("document");
        QTest::addColumn<QString>("expectedText");
        for (int r = 0; r < 4; ++r)
            for (double z : {.75, 1.5})
                for (bool cropped : {false, true})
                    QTest::newRow(
                        qPrintable(QStringLiteral("r%1-z%2-crop%3").arg(r).arg(z).arg(cropped)))
                        << r << z << QString(cropped ? "cropped-rotated.pdf" : "basic.pdf")
                        << QString(cropped ? "Crop text" : "Airlet test 1");
    }
    void selectionGeometryAndText() {
        QFETCH(int, rotation);
        QFETCH(double, zoom);
        QFETCH(QString, document);
        QFETCH(QString, expectedText);
        auto engine = makePdfiumEngine();
        const auto opened = engine->open(fixture(qPrintable(document)), {});
        const auto text = engine->text(0);
        QVERIFY(!text.glyphs.isEmpty());
        PdfView view;
        view.resize(900, 650);
        view.show();
        view.openFile(fixture(qPrintable(document)));
        QTRY_COMPARE(view.pageCount(), opened.pages);
        view.setZoom(zoom);
        view.rotate(rotation);
        view.goToPage(0);
        const auto first = text.glyphs.front().bounds.center();
        const auto last = text.glyphs.back().bounds.center();
        for (const auto point :
             {first, last,
              QPointF(opened.first.points.width() * .17, opened.first.points.height() * .47)}) {
            const auto back = view.viewportToPage(0, view.pageToViewport(0, point));
            QVERIFY(QLineF(back, point).length() < 1e-6);
        }
        dragSelection(view, view.pageToViewport(0, first), view.pageToViewport(0, last));
        QTRY_COMPARE_WITH_TIMEOUT(view.selectedText(), expectedText, 5000);
        const auto output = qEnvironmentVariable("AIRLET_TEST_OUTPUT");
        if (!output.isEmpty() && document == "basic.pdf" && rotation == 0 && zoom == .75)
            view.grab().save(output + "/text-selection.png");
        QApplication::clipboard()->clear();
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), expectedText);
        dragSelection(view, view.pageToViewport(0, last), view.pageToViewport(0, first));
        QTRY_COMPARE_WITH_TIMEOUT(view.selectedText(), expectedText, 5000);
        const auto selected = view.selectedText();
        view.setZoom(zoom * 1.1);
        view.rotate(1);
        QCOMPARE(view.selectedText(), selected);
        view.closeDocument();
        QCOMPARE(view.selectedText(), QString());
    }
    void cropRotations_data() {
        QTest::addColumn<int>("rotation");
        for (int r = 0; r < 4; ++r)
            QTest::newRow(qPrintable(QString::number(r))) << r;
    }
    void cropRotations() {
        QFETCH(int, rotation);
        PdfView view;
        view.resize(900, 650);
        view.show();
        view.openFile(fixture("basic.pdf"));
        QTRY_COMPARE(view.pageCount(), 3);
        view.setZoom(.75);
        view.rotate(rotation);
        view.goToPage(0);
        view.setSelectionMode(PdfView::SelectionMode::Area);
        dragSelection(view, view.pageToViewport(0, QPointF(80, 100)),
                      view.pageToViewport(0, QPointF(220, 290)));
        QVERIFY(view.hasAreaSelection());
        QSignalSpy ready(&view, &PdfView::selectionImageReady);
        const auto id = view.requestSelectionImage();
        QVERIFY(id > 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ready[0][0].toULongLong(), id);
        QVERIFY(ready[0][2].toString().isEmpty());
        const auto image = qvariant_cast<QImage>(ready[0][1]);
        QVERIFY(!image.isNull());
        const auto expected = rotatedSize(QSizeF(280, 380), rotation).toSize();
        QVERIFY(std::abs(image.width() - expected.width()) <= 3);
        QVERIFY(std::abs(image.height() - expected.height()) <= 3);
        QVERIFY(image.pixelColor(10, 10).blue() > 180);
        QVERIFY(image.pixelColor(10, 10).red() < 70);
        const auto output = qEnvironmentVariable("AIRLET_TEST_OUTPUT");
        if (!output.isEmpty()) {
            view.grab().save(output + QStringLiteral("/selection-r%1.png").arg(rotation));
            image.save(output + QStringLiteral("/crop-r%1.png").arg(rotation));
        }
    }
    void lazyTextAndCropService() {
        RenderService service;
        QSignalSpy opened(&service, &RenderService::opened),
            texts(&service, &RenderService::textReady), areas(&service, &RenderService::areaReady);
        service.open(1, fixture("basic.pdf"));
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(service.jobsStarted(), quint64(0)); // no automatic text scan
        service.requestText(1, 0, 1);
        QTRY_COMPARE(texts.size(), 1);
        const auto text = qvariant_cast<TextPage>(texts[0][3]);
        QVERIFY(!text.glyphs.isEmpty());
        RenderRequest r{0, 0, QSize(600, 800), QRect(100, 100, 450, 600), false, {}};
        service.requestArea(1, 2, r);
        QTRY_COMPARE_WITH_TIMEOUT(areas.size(), 1, 5000);
        auto engine = makePdfiumEngine();
        engine->open(fixture("basic.pdf"), {});
        QCOMPARE(qvariant_cast<QImage>(areas[0][2]), engine->render(r, [] { return false; }));
        const auto jobs = service.jobsStarted();
        QEventLoop idle;
        QTimer::singleShot(150, &idle, &QEventLoop::quit);
        idle.exec();
        QCOMPARE(service.jobsStarted(), jobs);
    }
    void cancelAreaOnDocumentChange() {
        auto state = std::make_shared<SlowState>();
        RenderService service(nullptr, [state] { return std::make_unique<SlowEngine>(state); });
        QSignalSpy opened(&service, &RenderService::opened),
            areas(&service, &RenderService::areaReady);
        service.open(1, "one");
        QTRY_COMPARE(opened.size(), 1);
        service.requestArea(1, 1, {0, 0, QSize(300, 400), QRect(0, 0, 300, 400), false, {}});
        QTRY_COMPARE(state->started.load(), 1);
        service.open(2, "two");
        QTRY_COMPARE(opened.size(), 2);
        QCOMPARE(state->cancelled.load(), 1);
        QCOMPARE(areas.size(), 0);
    }
    void imageSaveAtomicAndOriginalProtection() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto source = dir.filePath("source.pdf");
        QVERIFY(QFile::copy(fixture("basic.pdf"), source));
        QFile original(source);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();
        QImage image(120, 90, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        ImageSaveJob job;
        QSignalSpy finished(&job, &ImageSaveJob::finished);
        const auto png = dir.filePath("crop.png");
        QVERIFY(job.start(image, png, "PNG", source, false, 1));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished[0][1].toBool());
        QCOMPARE(QImage(png).convertToFormat(QImage::Format_ARGB32), image);
        QVERIFY(job.start(image, source, "PNG", source, true, 2));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!finished[1][1].toBool());
        QFile check(source);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), bytes);
        QImage changed = image;
        changed.fill(Qt::red);
        QVERIFY(job.start(changed, png, "PNG", source, false, 3));
        QTRY_COMPARE(finished.size(), 3);
        QVERIFY(!finished[2][1].toBool());
        QCOMPARE(QImage(png).pixelColor(5, 5), QColor(Qt::blue));
        const auto jpg = dir.filePath("crop.jpg");
        QVERIFY(job.start(image, jpg, "JPG", source, false, 4));
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(finished[3][1].toBool());
        QCOMPARE(QImage(jpg).size(), image.size());
        const auto cancelled = dir.filePath("cancelled.png");
        QImage large(4096, 4096, QImage::Format_ARGB32);
        large.fill(Qt::green);
        QVERIFY(job.start(large, cancelled, "PNG", source, false, 5));
        job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 5, 5000);
        QVERIFY(finished[4][2].toBool());
        QVERIFY(!QFile::exists(cancelled));
    }
};
QTEST_MAIN(TestAirlet)
#include "TestAirlet.moc"
