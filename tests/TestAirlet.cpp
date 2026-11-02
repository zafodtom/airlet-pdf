#include "core/Geometry.h"
#include "core/PdfEngine.h"
#include "core/Selection.h"
#include "export/ImageSaveJob.h"
#include "export/PageExport.h"
#include "render/RenderService.h"
#include "render/TileCache.h"
#include "ui/MainWindow.h"
#include "ui/PdfView.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
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
                  Qt::KeyboardModifiers modifiers = {}) {
    const auto local = view.viewport()->rect().center();
    QWheelEvent event(local, view.viewport()->mapToGlobal(local), pixels, QPoint(0, angle),
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
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
