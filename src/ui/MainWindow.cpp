#include "MainWindow.h"
#include "DocumentTools.h"
#include "PdfView.h"
#include "export/PageExport.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

namespace airlet {
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), view(new PdfView(this)) {
    setWindowTitle(QStringLiteral("Airlet PDF"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/airlet.png")));
    resize(1050, 800);
    setCentralWidget(view);
    tools = new DocumentTools(this, view);
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
    auto menuAction = [&](const QString &text, const QKeySequence &shortcut, auto fn) {
        auto *a = new QAction(text, this);
        a->setShortcut(shortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    auto *openAction =
        menuAction(QStringLiteral("Otevřít…"), QKeySequence::Open, [this] { chooseFile(); });
    auto *close = menuAction(QStringLiteral("Zavřít"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
        view->closeDocument();
        openedPath.clear();
        setWindowTitle("Airlet PDF");
    });
    auto *exportAction = menuAction(QStringLiteral("Export…"), QKeySequence(Qt::CTRL | Qt::Key_E),
                                    [this] { exportPages(); });
    auto *findAction = new QAction(QStringLiteral("Hledat…"), this);
    findAction->setShortcut(QKeySequence::Find);
    addAction(findAction);
    connect(findAction, &QAction::triggered, tools, &DocumentTools::find);
    auto *printAction = new QAction(QStringLiteral("Tisk…"), this);
    printAction->setShortcut(QKeySequence::Print);
    addAction(printAction);
    connect(printAction, &QAction::triggered, tools, &DocumentTools::print);
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("Soubor"));
    fileMenu->addAction(openAction);
    auto *imagesAction = fileMenu->addAction(QStringLiteral("PDF z obrázků…"));
    connect(imagesAction, &QAction::triggered, tools, &DocumentTools::imagesPdf);
    fileMenu->addAction(exportAction);
    fileMenu->addAction(printAction);
    fileMenu->addAction(close);
    menuBar()->addMenu(QStringLiteral("Úpravy"))->addAction(findAction);
    QList<QAction *> documentActions{close, exportAction, findAction, printAction};
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
    connect(page, &QSpinBox::editingFinished, this, [this] {
        view->goToPage(page->value() - 1);
        view->setFocus();
    });
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
    bar->addSeparator();
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    auto *textMode = action(QStringLiteral("Text"), QKeySequence(Qt::ALT | Qt::Key_T),
                            [this] { view->setSelectionMode(PdfView::SelectionMode::Text); });
    auto *areaMode = action(QStringLiteral("Výřez"), QKeySequence(Qt::ALT | Qt::Key_X),
                            [this] { view->setSelectionMode(PdfView::SelectionMode::Area); });
    for (auto *a : {textMode, areaMode}) {
        a->setCheckable(true);
        modeGroup->addAction(a);
        documentActions << a;
    }
    textMode->setChecked(true);
    view->setSelectionMode(PdfView::SelectionMode::Text);
    textMode->setToolTip(QStringLiteral("Vybrat text tažením, Ctrl+C kopíruje (jedna stránka)"));
    areaMode->setToolTip(QStringLiteral("Vybrat obdélníkový výřez, Ctrl+C kopíruje obrázek"));
    copyAction = new QAction(QStringLiteral("Kopírovat výběr"), view);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, &MainWindow::copySelection);
    saveAreaAction = new QAction(QStringLiteral("Uložit výřez…"), this);
    connect(saveAreaAction, &QAction::triggered, this, &MainWindow::saveArea);
    copyAction->setEnabled(false);
    saveAreaAction->setEnabled(false);
    connect(view, &PdfView::selectionChanged, this, [this] {
        copyAction->setEnabled(!view->selectedText().isEmpty() || view->hasAreaSelection());
        saveAreaAction->setEnabled(view->hasAreaSelection());
    });
    connect(view, &PdfView::selectionNotice, this,
            [this](const QString &text) { statusBar()->showMessage(text); });
    connect(view, &PdfView::selectionImageProgress, this, [this](quint64 id, int percent) {
        if (id == pendingImage && cropProgress)
            cropProgress->setValue(percent * 8 / 10);
    });
    connect(
        view, &PdfView::selectionImageReady, this, [this](quint64 id, QImage image, QString error) {
            if (id != pendingImage)
                return;
            if (!error.isEmpty() || image.isNull()) {
                endAreaOperation();
                QMessageBox::warning(this, QStringLiteral("Výřez"),
                                     error.isEmpty() ? QStringLiteral("Výřez je prázdný.") : error);
                return;
            }
            if (pendingSavePath.isEmpty()) {
                QApplication::clipboard()->setImage(image);
                endAreaOperation();
                statusBar()->showMessage(QStringLiteral("Výřez %1 × %2 px je ve schránce.")
                                             .arg(image.width())
                                             .arg(image.height()));
            } else {
                if (cropProgress) {
                    cropProgress->setLabelText(QStringLiteral("Ukládám výřez…"));
                    cropProgress->setRange(0, 0);
                }
                if (!imageSaver.start(image, pendingSavePath, pendingSaveFormat, pendingSaveSource,
                                      pendingOverwrite, id)) {
                    endAreaOperation();
                    statusBar()->showMessage(QStringLiteral("Předchozí ukládání ještě končí."));
                }
            }
        });
    connect(&imageSaver, &ImageSaveJob::finished, this,
            [this](quint64 id, bool success, bool cancelled, QString error) {
                if (id != pendingImage)
                    return;
                const auto target = pendingSavePath;
                endAreaOperation();
                if (success)
                    statusBar()->showMessage(
                        QStringLiteral("Výřez byl uložen: %1").arg(QFileInfo(target).fileName()));
                else if (cancelled)
                    statusBar()->showMessage(QStringLiteral("Ukládání výřezu bylo zrušeno."));
                else
                    QMessageBox::warning(this, QStringLiteral("Uložení výřezu"), error);
            });
    auto *aboutAction = menuAction(QStringLiteral("O aplikaci"), {}, [this] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Airlet PDF"));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral(
                "<b>Airlet PDF %1</b><p>Rychlý lokální PDF prohlížeč.</p>"
                "<p>Qt %2, dynamicky připojené knihovny pod LGPLv3.<br>PDFium "
                "chromium/8125, BSD a licence třetích stran.</p>"
                "<p>Oznámení a licence jsou ve složce licenses vedle aplikace. "
                "Kompatibilní Qt DLL lze nahradit; "
                "zpětné inženýrství pro ladění změn LGPL knihoven je dovoleno.</p>"
                "<p>Text: výběr tažením a Ctrl+C. Výřez: obdélník do schránky nebo PNG/JPG. "
                "Výběr je omezen na jednu stránku, bez OCR.</p>"
                "<p>Ctrl+F: hledání textu. Ctrl+P: tisk. Soubor → PDF z obrázků: "
                "nové PDF ze sady obrázků. Otočení mění pouze zobrazení.</p>")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
        box.exec();
    });
    menuBar()->addMenu(QStringLiteral("Nápověda"))->addAction(aboutAction);
    for (auto *a : documentActions)
        a->setEnabled(false);
    page->setEnabled(false);
    connect(view, &PdfView::pageChanged, this, [this, documentActions](int current, int total) {
        if (total == 0)
            cancelAreaOperation();
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
    connect(
        view, &QWidget::customContextMenuRequested, this, [this, textMode, areaMode](QPoint point) {
            QMenu menu(this);
            if (view->pageCount()) {
                menu.addAction(copyAction);
                menu.addAction(saveAreaAction);
                menu.addSeparator();
                menu.addAction(QStringLiteral("Vybrat text"), textMode, &QAction::trigger);
                menu.addAction(QStringLiteral("Vybrat výřez"), areaMode, &QAction::trigger);
                menu.addAction(QStringLiteral("Zrušit výběr"), view, &PdfView::clearSelection);
                menu.addSeparator();
                menu.addAction(QStringLiteral("Šířka stránky"), view, &PdfView::fitWidth);
                menu.addAction(QStringLiteral("Celá stránka"), view, &PdfView::fitPage);
                menu.addAction(QStringLiteral("Otočit doprava"), this, [this] { view->rotate(1); });
            }
            menu.exec(view->mapToGlobal(point));
        });
}
void MainWindow::copySelection() {
    const auto text = view->selectedText();
    if (!text.isEmpty()) {
        if (pendingImage && pendingSavePath.isEmpty())
            cancelAreaOperation();
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(QStringLiteral("Text je ve schránce."));
        return;
    }
    if (!view->hasAreaSelection())
        return;
    if (imageSaver.isRunning() || pendingImage) {
        statusBar()->showMessage(QStringLiteral("Nejprve dokončete nebo zrušte předchozí výřez."));
        return;
    }
    pendingSavePath.clear();
    beginAreaOperation();
}
void MainWindow::saveArea() {
    if (!view->hasAreaSelection())
        return;
    if (imageSaver.isRunning() || pendingImage) {
        statusBar()->showMessage(QStringLiteral("Nejprve dokončete nebo zrušte předchozí výřez."));
        return;
    }
    QString filter = QStringLiteral("PNG (*.png)");
    const auto name = QFileInfo(openedPath).completeBaseName() + QStringLiteral("_vyrez.png");
    auto target = QFileDialog::getSaveFileName(this, QStringLiteral("Uložit výřez"),
                                               QDir(lastFolder).filePath(name),
                                               QStringLiteral("PNG (*.png);;JPG (*.jpg *.jpeg)"),
                                               &filter, QFileDialog::DontConfirmOverwrite);
    if (target.isEmpty())
        return;
    if (QFileInfo(target).suffix().isEmpty())
        target += filter.startsWith("PNG") ? ".png" : ".jpg";
    const auto ext = QFileInfo(target).suffix().toLower();
    if (ext != "png" && ext != "jpg" && ext != "jpeg") {
        QMessageBox::warning(
            this, QStringLiteral("Výřez"),
            QStringLiteral("Zvolte soubor PNG nebo JPG. PDF dokument se nepřepisuje."));
        return;
    }
    const bool exists = QFileInfo::exists(target);
    if (exists && QMessageBox::question(this, QStringLiteral("Přepsat obrázek?"),
                                        QStringLiteral("Soubor %1 už existuje. Chcete jej přepsat?")
                                            .arg(QFileInfo(target).fileName()),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No) != QMessageBox::Yes)
        return;
    pendingSavePath = target;
    pendingSaveSource = openedPath;
    pendingSaveFormat = ext == "png" ? "PNG" : "JPG";
    pendingOverwrite = exists;
    beginAreaOperation();
}
void MainWindow::beginAreaOperation() {
    pendingImage = view->requestSelectionImage();
    if (!pendingImage)
        return;
    cropProgress = new QProgressDialog(QStringLiteral("Vytvářím výřez…"), QStringLiteral("Zrušit"),
                                       0, 100, this);
    cropProgress->setWindowModality(Qt::NonModal);
    cropProgress->setMinimumDuration(0);
    cropProgress->setAutoClose(false);
    cropProgress->setAutoReset(false);
    connect(cropProgress, &QProgressDialog::canceled, this, &MainWindow::cancelAreaOperation);
    cropProgress->show();
}
void MainWindow::cancelAreaOperation() {
    if (!pendingImage)
        return;
    view->cancelSelectionImage();
    imageSaver.cancel();
    endAreaOperation();
    statusBar()->showMessage(QStringLiteral("Požadavek na výřez byl zrušen."));
}
void MainWindow::endAreaOperation() {
    pendingImage = 0;
    pendingSavePath.clear();
    if (cropProgress) {
        cropProgress->hide();
        cropProgress->deleteLater();
        cropProgress = nullptr;
    }
}
MainWindow::~MainWindow() {
    delete tools;
    exportProcess.disconnect(this);
    if (exportProcess.state() != QProcess::NotRunning) {
        exportProcess.kill();
        exportProcess.waitForFinished(3000);
    }
}
void MainWindow::exportPages() {
    if (openedPath.isEmpty())
        return;
    if (exportProcess.state() != QProcess::NotRunning) {
        statusBar()->showMessage(QStringLiteral("Export už probíhá."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Export stránek do obrázků"));
    QFormLayout form(&dialog);
    QComboBox scope;
    scope.addItems({QStringLiteral("Aktuální stránka"), QStringLiteral("Všechny stránky"),
                    QStringLiteral("Vlastní rozsah")});
    QLineEdit range;
    range.setPlaceholderText(QStringLiteral("1–5, 8, 12"));
    range.setEnabled(false);
    connect(&scope, qOverload<int>(&QComboBox::currentIndexChanged), &range,
            [&range](int i) { range.setEnabled(i == 2); });
    QComboBox format;
    format.addItems({"PNG", "JPG"});
    QSpinBox dpi;
    dpi.setRange(72, 600);
    dpi.setValue(150);
    QSpinBox quality;
    quality.setRange(1, 100);
    quality.setValue(92);
    quality.setEnabled(false);
    connect(&format, qOverload<int>(&QComboBox::currentIndexChanged), &quality,
            [&quality](int i) { quality.setEnabled(i == 1); });
    form.addRow(QStringLiteral("Stránky:"), &scope);
    form.addRow(QStringLiteral("Rozsah:"), &range);
    form.addRow(QStringLiteral("Formát:"), &format);
    form.addRow(QStringLiteral("DPI:"), &dpi);
    form.addRow(QStringLiteral("Kvalita JPG:"), &quality);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto specification = scope.currentIndex() == 0 ? QString::number(view->currentPage() + 1)
                               : scope.currentIndex() == 1
                                   ? QStringLiteral("1-%1").arg(view->pageCount())
                                   : range.text();
    QString error;
    const auto pages = parsePageRange(specification, view->pageCount(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Rozsah"), error);
        return;
    }
    const auto parent = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Nadřazená složka exportu (vytvoří se složka podle PDF)"), lastFolder);
    if (parent.isEmpty())
        return;
    const auto output = QDir(parent).filePath(QFileInfo(openedPath).completeBaseName());
    if (!QDir().mkpath(output)) {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("Složku nelze vytvořit."));
        return;
    }
    exportTemporary =
        std::make_unique<QTemporaryDir>(QDir(output).filePath(".airlet-export-XXXXXX"));
    if (!exportTemporary->isValid()) {
        exportTemporary.reset();
        return;
    }
    QJsonArray rotations;
    for (int r : view->pageRotations())
        rotations.append(r);
    const QJsonObject job{{"source", openedPath},       {"password", view->exportPassword()},
                          {"range", specification},     {"dpi", dpi.value()},
                          {"quality", quality.value()}, {"format", format.currentText()},
                          {"output", output},           {"temporary", exportTemporary->path()},
                          {"rotations", rotations}};
    exportCompleted = 0;
    exportError.clear();
    exportCancelled = false;
    exportBuffer.clear();
    exportProgress = new QProgressDialog(QStringLiteral("Exportuji stránky…"),
                                         QStringLiteral("Zrušit"), 0, pages.size(), this);
    exportProgress->setWindowModality(Qt::NonModal);
    exportProgress->setMinimumDuration(0);
    exportProgress->setAutoClose(false);
    exportProgress->setAutoReset(false);
    connect(exportProgress, &QProgressDialog::canceled, this, [this] {
        exportCancelled = true;
        exportProcess.kill();
    });
    exportProcess.disconnect(this);
    connect(&exportProcess, &QProcess::readyReadStandardOutput, this, [this] {
        exportBuffer += exportProcess.readAllStandardOutput();
        int end;
        while ((end = exportBuffer.indexOf('\n')) >= 0) {
            const auto record = QJsonDocument::fromJson(exportBuffer.left(end)).object();
            exportBuffer.remove(0, end + 1);
            if (record.contains("error"))
                exportError = record["error"].toString();
            if (record.contains("completed")) {
                exportCompleted = record["completed"].toInt();
                if (exportProgress)
                    exportProgress->setValue(exportCompleted);
            }
        }
    });
    connect(
        &exportProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, output](int code, QProcess::ExitStatus status) {
            if (exportProgress) {
                exportProgress->hide();
                exportProgress->deleteLater();
                exportProgress = nullptr;
            }
            exportTemporary.reset();
            const auto message =
                QStringLiteral("Dokončeno %1 stran. Složka: %2").arg(exportCompleted).arg(output);
            if (exportCancelled)
                QMessageBox::information(this, QStringLiteral("Export zrušen"),
                                         message +
                                             QStringLiteral("\nHotové obrázky zůstaly zachovány, "
                                                            "nedokončený výstup byl odstraněn."));
            else if (code != 0 || status != QProcess::NormalExit)
                QMessageBox::warning(this, QStringLiteral("Export nebyl dokončen"),
                                     message + "\n" +
                                         (exportError.isEmpty()
                                              ? QStringLiteral("Pomocný proces skončil chybou.")
                                              : exportError));
            else
                QMessageBox::information(this, QStringLiteral("Export dokončen"), message);
        });
    connect(&exportProcess, &QProcess::started, this, [this, job] {
        exportProcess.write(QJsonDocument(job).toJson(QJsonDocument::Compact) + "\n");
        exportProcess.closeWriteChannel();
    });
    connect(&exportProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (exportProgress) {
                exportProgress->hide();
                exportProgress->deleteLater();
                exportProgress = nullptr;
            }
            exportTemporary.reset();
            QMessageBox::warning(this, QStringLiteral("Export"), exportProcess.errorString());
        }
    });
#ifdef Q_OS_WIN
    const auto workerName = QStringLiteral("AirletExport.exe");
#else
    const auto workerName = QStringLiteral("AirletExport");
#endif
    exportProcess.start(QDir(QCoreApplication::applicationDirPath()).filePath(workerName), {});
    exportProgress->show();
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
    exportProcess.disconnect(this);
    if (exportProcess.state() != QProcess::NotRunning) {
        exportProcess.kill();
        exportProcess.waitForFinished(3000);
    }
    exportTemporary.reset();
    QSettings settings;
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("lastFolder", lastFolder);
    view->closeDocument();
    QMainWindow::closeEvent(e);
}
} // namespace airlet
