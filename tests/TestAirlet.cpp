#include "core/Geometry.h"
#include "core/PdfEngine.h"
#include "render/RenderService.h"
#include "render/TileCache.h"
#include "ui/MainWindow.h"
#include "ui/PdfView.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <ctime>
#include <thread>

using namespace airlet;
static QString fixture(const char *name) {
    return QString::fromUtf8(FIXTURE_DIR) + "/" + name;
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
    void initTestCase() {
        qRegisterMetaType<OpenResult>();
        qRegisterMetaType<RenderRequest>();
        qRegisterMetaType<PageInfo>();
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
};
QTEST_MAIN(TestAirlet)
#include "TestAirlet.moc"
