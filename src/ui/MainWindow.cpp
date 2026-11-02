#include "MainWindow.h"
#include "PdfView.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

namespace airlet {
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), view(new PdfView(this)) {
    setWindowTitle(QStringLiteral("Airlet PDF"));
    resize(1050, 800);
    setCentralWidget(view);
    QSettings settings;
    restoreGeometry(settings.value("windowGeometry").toByteArray());
    lastFolder = settings.value("lastFolder").toString();
    auto *bar = addToolBar(QStringLiteral("Dokument"));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    auto action = [&](const QString &text, const QKeySequence &shortcut, auto fn) {
        auto *a = bar->addAction(text);
        a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action(QStringLiteral("Otevřít…"), QKeySequence::Open, [this] { chooseFile(); });
    auto *close = action(QStringLiteral("Zavřít"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
        view->closeDocument();
        openedPath.clear();
        setWindowTitle("Airlet PDF");
    });
    bar->addSeparator();
    QList<QAction *> documentActions{close};
    documentActions << action(QStringLiteral("|◀"), QKeySequence(Qt::CTRL | Qt::Key_Home),
                              [this] { view->goToPage(0); });
    documentActions << action(QStringLiteral("◀"), QKeySequence(Qt::Key_PageUp),
                              [this] { view->goToPage(view->currentPage() - 1); });
    page = new QSpinBox(bar);
    page->setRange(1, 1);
    page->setKeyboardTracking(false);
    page->setToolTip(QStringLiteral("Přejít na stránku"));
    bar->addWidget(page);
    count = new QLabel(QStringLiteral(" / 0  "), bar);
    bar->addWidget(count);
    connect(page, &QSpinBox::editingFinished, this, [this] { view->goToPage(page->value() - 1); });
    documentActions << action(QStringLiteral("▶"), QKeySequence(Qt::Key_PageDown),
                              [this] { view->goToPage(view->currentPage() + 1); });
    documentActions << action(QStringLiteral("▶|"), QKeySequence(Qt::CTRL | Qt::Key_End),
                              [this] { view->goToPage(view->pageCount() - 1); });
    bar->addSeparator();
    documentActions << action(QStringLiteral("−"), QKeySequence::ZoomOut,
                              [this] { view->setZoom(view->zoom() / 1.15); });
    documentActions << action(QStringLiteral("+"), QKeySequence::ZoomIn,
                              [this] { view->setZoom(view->zoom() * 1.15); });
    documentActions << action(QStringLiteral("100 %"), QKeySequence(Qt::CTRL | Qt::Key_0),
                              [this] { view->setZoom(1.); });
    documentActions << action(QStringLiteral("Šířka"), QKeySequence(Qt::CTRL | Qt::Key_1),
                              [this] { view->fitWidth(); });
    documentActions << action(QStringLiteral("Stránka"), QKeySequence(Qt::CTRL | Qt::Key_2),
                              [this] { view->fitPage(); });
    bar->addSeparator();
    auto *rotateLeft = action(QStringLiteral("↶"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                              [this] { view->rotate(-1); });
    auto *rotateRight = action(QStringLiteral("↷"), QKeySequence(Qt::CTRL | Qt::Key_R),
                               [this] { view->rotate(1); });
    rotateLeft->setToolTip(QStringLiteral("Otočit doleva (pouze zobrazení)"));
    rotateRight->setToolTip(QStringLiteral("Otočit doprava (pouze zobrazení)"));
    documentActions << rotateLeft << rotateRight;
    action(QStringLiteral("O aplikaci"), {}, [this] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Airlet PDF"));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral("<b>Airlet PDF %1</b><p>Rychlý lokální PDF prohlížeč. Milník 1.</p>"
                           "<p>Qt %2, dynamicky připojené knihovny pod LGPLv3.<br>PDFium "
                           "chromium/8125, BSD a licence třetích stran.</p>"
                           "<p>Oznámení a licence jsou ve složce licenses vedle aplikace. "
                           "Kompatibilní Qt DLL lze nahradit; "
                           "zpětné inženýrství pro ladění změn LGPL knihoven je dovoleno.</p>"
                           "<p>Otočení je zatím pouze pro zobrazení. Uložení, výběr, tisk a "
                           "exporty budou v dalších milnících.</p>")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
        box.exec();
    });
    for (auto *a : documentActions)
        a->setEnabled(false);
    page->setEnabled(false);
    connect(view, &PdfView::pageChanged, this, [this, documentActions](int current, int total) {
        const QSignalBlocker blocker(page);
        page->setMaximum(std::max(1, total));
        page->setValue(std::max(1, current));
        count->setText(QStringLiteral(" / %1  ").arg(total));
        page->setEnabled(total > 0);
        for (auto *a : documentActions)
            a->setEnabled(total > 0);
    });
    connect(view, &PdfView::fileDropped, this, &MainWindow::openFile);
    connect(view, &PdfView::documentOpened, this, [this](QString p, int) {
        openedPath = p;
        lastFolder = QFileInfo(p).absolutePath();
        setWindowTitle(QFileInfo(p).fileName() + QStringLiteral(" — Airlet PDF"));
        statusBar()->showMessage(QStringLiteral(
            "Otočení mění pouze zobrazení. Ctrl+kolečko: zoom; prostřední tlačítko: posun."));
    });
    connect(view, &PdfView::openFailed, this, [this](QString p, OpenResult result) {
        if (result.error == OpenError::Password) {
            bool ok = false;
            const auto password = QInputDialog::getText(this, QStringLiteral("Chráněné PDF"),
                                                        QStringLiteral("Heslo dokumentu:"),
                                                        QLineEdit::Password, {}, &ok);
            if (ok)
                view->openFile(p, password);
        } else
            QMessageBox::warning(this, QStringLiteral("Airlet PDF"), result.message);
    });
    connect(view, &PdfView::firstImage, this, [this](qint64 ms) {
        if (QCoreApplication::arguments().contains("--metrics"))
            qInfo("first_image_from_open_ms=%lld", static_cast<long long>(ms));
        statusBar()->showMessage(
            QStringLiteral("První obraz za %1 ms · Ctrl+kolečko: zoom · prostřední tlačítko: posun")
                .arg(ms));
    });
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested, this, [this](QPoint point) {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Otevřít…"), this, &MainWindow::chooseFile);
        if (view->pageCount()) {
            menu.addAction(QStringLiteral("Šířka stránky"), view, &PdfView::fitWidth);
            menu.addAction(QStringLiteral("Celá stránka"), view, &PdfView::fitPage);
            menu.addAction(QStringLiteral("Otočit doprava"), this, [this] { view->rotate(1); });
        }
        menu.exec(view->mapToGlobal(point));
    });
}
void MainWindow::chooseFile() {
    const auto p =
        QFileDialog::getOpenFileName(this, QStringLiteral("Otevřít PDF"), lastFolder,
                                     QStringLiteral("PDF dokumenty (*.pdf);;Všechny soubory (*)"));
    if (!p.isEmpty())
        openFile(p);
}
void MainWindow::openFile(const QString &p) {
    if (!p.isEmpty()) {
        openedPath.clear();
        setWindowTitle(QStringLiteral("Airlet PDF"));
        view->openFile(QFileInfo(p).absoluteFilePath());
    }
}
void MainWindow::closeEvent(QCloseEvent *e) {
    QSettings settings;
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("lastFolder", lastFolder);
    view->closeDocument();
    QMainWindow::closeEvent(e);
}
} // namespace airlet
