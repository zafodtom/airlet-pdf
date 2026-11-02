#include "MainWindow.h"
#include "DocumentTools.h"
#include "PdfView.h"
#include "export/PageExport.h"
#include "i18n/Language.h"
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
#include <QFile>
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
#include <QTextBrowser>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

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
    auto *languageMenu = new QMenu(uiText(QStringLiteral("Jazyk")), this);
    auto *languages = new QActionGroup(this);
    for (const auto &entry : {qMakePair(uiText(QStringLiteral("Čeština")), QStringLiteral("cs")),
                              qMakePair(QStringLiteral("English"), QStringLiteral("en"))}) {
        auto *a = languageMenu->addAction(entry.first);
        a->setCheckable(true);
        a->setChecked(settings.value("language", "cs").toString() == entry.second);
        languages->addAction(a);
        connect(a, &QAction::triggered, this, [this, code = entry.second] {
            QSettings().setValue("language", code);
            QMessageBox::information(
                this, uiText(QStringLiteral("Jazyk")),
                uiText(QStringLiteral("Změna jazyka se projeví po novém spuštění aplikace.")));
        });
    }
    auto *settingsMenu = new QMenu(uiText(QStringLiteral("Nastavení")), this);
    settingsMenu->addMenu(languageMenu);
    auto *updatesAction = settingsMenu->addAction(uiText(QStringLiteral("Aktualizace…")));
    connect(updatesAction, &QAction::triggered, this, &MainWindow::checkUpdates);
    auto *bar = addToolBar(uiText(QStringLiteral("Dokument")));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    continueAction = settingsMenu->addAction(uiText(QStringLiteral("Pokračovat dalším souborem")));
    continueAction->setObjectName("continueDocuments");
    settingsMenu->removeAction(updatesAction);
    settingsMenu->addSeparator();
    settingsMenu->addAction(updatesAction);
    continueAction->setCheckable(true);
    continueAction->setChecked(false);
    continueAction->setToolTip(
        uiText(QStringLiteral("Na začátku či konci otevřít sousední PDF ze stejné složky. Po "
                              "spuštění je vždy vypnuto.")));
    connect(continueAction, &QAction::toggled, this, [this](bool enabled) {
        pendingFolderRequest = 0;
        view->setContinueToNextDocument(enabled);
    });
    connect(view, &PdfView::nextDocumentRequested, this, [this] {
        if (continueAction->isChecked() && !openedPath.isEmpty() && !pendingFolderRequest) {
            pendingFolderDirection = 1;
            pendingFolderRequest = folderNavigator.request(openedPath, 1);
        }
    });
    connect(view, &PdfView::previousDocumentRequested, this, [this] {
        if (continueAction->isChecked() && !openedPath.isEmpty() && !pendingFolderRequest) {
            pendingFolderDirection = -1;
            pendingFolderRequest = folderNavigator.request(openedPath, -1);
        }
    });
    connect(&folderNavigator, &FolderNavigator::resolved, this,
            [this](quint64 id, QString source, QString next) {
                if (id != pendingFolderRequest || !continueAction->isChecked() ||
                    openedPath != source)
                    return;
                pendingFolderRequest = 0;
                if (next.isEmpty()) {
                    statusBar()->showMessage(
                        pendingFolderDirection > 0
                            ? uiText(QStringLiteral("Ve složce už není další PDF."))
                            : uiText(QStringLiteral("Ve složce není předchozí PDF.")));
                    return;
                }
                openDocument(next, pendingFolderDirection < 0);
                automaticOpening = true;
                view->suppressNavigationInput();
            });
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
    auto *openAction = menuAction(uiText(QStringLiteral("Otevřít…")), QKeySequence::Open,
                                  [this] { chooseFile(); });
    auto *close =
        menuAction(uiText(QStringLiteral("Zavřít")), QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
            view->closeDocument();
            openedPath.clear();
            setWindowTitle("Airlet PDF");
        });
    auto *exportAction = menuAction(QStringLiteral("Export…"), QKeySequence(Qt::CTRL | Qt::Key_E),
                                    [this] { exportPages(); });
    auto *findAction = new QAction(uiText(QStringLiteral("Hledat…")), this);
    findAction->setShortcut(QKeySequence::Find);
    addAction(findAction);
    connect(findAction, &QAction::triggered, tools, &DocumentTools::find);
    auto *printAction = new QAction(uiText(QStringLiteral("Tisk…")), this);
    printAction->setShortcut(QKeySequence::Print);
    addAction(printAction);
    connect(printAction, &QAction::triggered, tools, &DocumentTools::print);
    auto *fileMenu = menuBar()->addMenu(uiText(QStringLiteral("Soubor")));
    fileMenu->addAction(openAction);
    auto *imagesAction = fileMenu->addAction(uiText(QStringLiteral("PDF z obrázků…")));
    connect(imagesAction, &QAction::triggered, tools, &DocumentTools::imagesPdf);
    fileMenu->addAction(exportAction);
    fileMenu->addAction(printAction);
    fileMenu->addAction(close);
    menuBar()->addMenu(uiText(QStringLiteral("Úpravy")))->addAction(findAction);
    menuBar()->addMenu(settingsMenu);
    QList<QAction *> documentActions{close, exportAction, findAction, printAction};
    documentActions << action(QStringLiteral("|◀"), QKeySequence(Qt::CTRL | Qt::Key_Home),
                              [this] { view->goToPage(0); });
    documentActions << action(QStringLiteral("◀"), QKeySequence(Qt::Key_PageUp),
                              [this] { view->previousPage(); });
    page = new QSpinBox(bar);
    page->setRange(1, 1);
    page->setKeyboardTracking(false);
    page->setToolTip(uiText(QStringLiteral("Přejít na stránku")));
    bar->addWidget(page);
    count = new QLabel(QStringLiteral(" / 0  "), bar);
    bar->addWidget(count);
    connect(page, &QSpinBox::editingFinished, this, [this] {
        view->goToPage(page->value() - 1);
        view->setFocus();
    });
    documentActions << action(QStringLiteral("▶"), QKeySequence(Qt::Key_PageDown),
                              [this] { view->nextPage(); });
    documentActions << action(QStringLiteral("▶|"), QKeySequence(Qt::CTRL | Qt::Key_End),
                              [this] { view->goToPage(view->pageCount() - 1); });
    bar->addSeparator();
    documentActions << action(QStringLiteral("−"), QKeySequence::ZoomOut,
                              [this] { view->setZoom(view->zoom() / 1.15); });
    documentActions << action(QStringLiteral("+"), QKeySequence::ZoomIn,
                              [this] { view->setZoom(view->zoom() * 1.15); });
    auto *actualSize = action(QStringLiteral("100 %"), QKeySequence(Qt::CTRL | Qt::Key_0),
                              [this] { view->setZoom(1.); });
    auto *fitWidth = action(uiText(QStringLiteral("Šířka")), QKeySequence(Qt::CTRL | Qt::Key_1),
                            [this] { view->fitWidth(); });
    auto *fitPage = action(uiText(QStringLiteral("Stránka")), QKeySequence(Qt::CTRL | Qt::Key_2),
                           [this] { view->fitPage(); });
    actualSize->setObjectName("actualSize");
    fitWidth->setObjectName("fitWidth");
    fitPage->setObjectName("fitPage");
    auto *fitGroup = new QActionGroup(this);
    for (auto *a : {actualSize, fitWidth, fitPage}) {
        a->setCheckable(true);
        fitGroup->addAction(a);
        documentActions << a;
    }
    auto updateFitActions = [this, fitGroup, actualSize, fitWidth, fitPage] {
        fitGroup->setExclusive(false);
        actualSize->setChecked(view->viewMode() == PdfView::ViewMode::Manual &&
                               qAbs(view->zoom() - 1.) < 1e-8);
        fitWidth->setChecked(view->viewMode() == PdfView::ViewMode::Width);
        fitPage->setChecked(view->viewMode() == PdfView::ViewMode::Page);
        fitGroup->setExclusive(true);
    };
    connect(view, &PdfView::viewModeChanged, this, updateFitActions);
    connect(view, &PdfView::zoomChanged, this, updateFitActions);
    updateFitActions();
    bar->addSeparator();
    auto *rotateLeft = action(QStringLiteral("↶"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                              [this] { view->rotate(-1); });
    auto *rotateRight = action(QStringLiteral("↷"), QKeySequence(Qt::CTRL | Qt::Key_R),
                               [this] { view->rotate(1); });
    rotateLeft->setToolTip(uiText(QStringLiteral("Otočit doleva (pouze zobrazení)")));
    rotateRight->setToolTip(uiText(QStringLiteral("Otočit doprava (pouze zobrazení)")));
    documentActions << rotateLeft << rotateRight;
    bar->addSeparator();
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    auto *textMode = action(QStringLiteral("Text"), QKeySequence(Qt::ALT | Qt::Key_T),
                            [this] { view->setSelectionMode(PdfView::SelectionMode::Text); });
    auto *areaMode = action(uiText(QStringLiteral("Výřez")), QKeySequence(Qt::ALT | Qt::Key_X),
                            [this] { view->setSelectionMode(PdfView::SelectionMode::Area); });
    for (auto *a : {textMode, areaMode}) {
        a->setCheckable(true);
        modeGroup->addAction(a);
        documentActions << a;
    }
    textMode->setChecked(true);
    view->setSelectionMode(PdfView::SelectionMode::Text);
    textMode->setToolTip(
        uiText(QStringLiteral("Vybrat text tažením, Ctrl+C kopíruje (jedna stránka)")));
    areaMode->setToolTip(
        uiText(QStringLiteral("Vybrat obdélníkový výřez, Ctrl+C kopíruje obrázek")));
    copyAction = new QAction(uiText(QStringLiteral("Kopírovat výběr")), view);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view->addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, &MainWindow::copySelection);
    saveAreaAction = new QAction(uiText(QStringLiteral("Uložit výřez…")), this);
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
                QMessageBox::warning(this, uiText(QStringLiteral("Výřez")),
                                     error.isEmpty() ? uiText(QStringLiteral("Výřez je prázdný."))
                                                     : error);
                return;
            }
            if (pendingSavePath.isEmpty()) {
                QApplication::clipboard()->setImage(image);
                endAreaOperation();
                statusBar()->showMessage(uiText(QStringLiteral("Výřez %1 × %2 px je ve schránce."))
                                             .arg(image.width())
                                             .arg(image.height()));
            } else {
                if (cropProgress) {
                    cropProgress->setLabelText(uiText(QStringLiteral("Ukládám výřez…")));
                    cropProgress->setRange(0, 0);
                }
                if (!imageSaver.start(image, pendingSavePath, pendingSaveFormat, pendingSaveSource,
                                      pendingOverwrite, id)) {
                    endAreaOperation();
                    statusBar()->showMessage(
                        uiText(QStringLiteral("Předchozí ukládání ještě končí.")));
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
                    statusBar()->showMessage(uiText(QStringLiteral("Výřez byl uložen: %1"))
                                                 .arg(QFileInfo(target).fileName()));
                else if (cancelled)
                    statusBar()->showMessage(
                        uiText(QStringLiteral("Ukládání výřezu bylo zrušeno.")));
                else
                    QMessageBox::warning(this, uiText(QStringLiteral("Uložení výřezu")), error);
            });
    bar->addSeparator();
    bar->addAction(continueAction);
    auto *aboutAction = menuAction(uiText(QStringLiteral("O aplikaci")), {}, [this] {
        QMessageBox box(this);
        box.setWindowTitle(uiText(QStringLiteral("O aplikaci")));
        box.setIconPixmap(windowIcon().pixmap(64, 64));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral("<h3>Airlet PDF %1</h3><p>%2</p>"
                           "<p>© 2026 zafodtom &amp; Airlet PDF contributors</p>"
                           "<p>%3</p><p><a href=\"https://github.com/zafodtom/airlet-pdf\">%4</a>"
                           " · <a href=\"%5\">%6</a> · <a href=\"%7\">%8</a></p>")
                .arg(QCoreApplication::applicationVersion().toHtmlEscaped(),
                     uiText(QStringLiteral("Rychlý PDF prohlížeč pro Windows.")).toHtmlEscaped(),
                     uiText(QStringLiteral("Zdarma pro osobní i komerční použití. Licence MIT."))
                         .toHtmlEscaped(),
                     uiText(QStringLiteral("Web projektu")),
                     QUrl::fromLocalFile(QCoreApplication::applicationDirPath() + "/LICENSE")
                         .toString(QUrl::FullyEncoded),
                     uiText(QStringLiteral("Licence")),
                     QUrl::fromLocalFile(QCoreApplication::applicationDirPath() +
                                         "/licenses/THIRD-PARTY.md")
                         .toString(QUrl::FullyEncoded),
                     uiText(QStringLiteral("Knihovny třetích stran"))));
        for (auto *label : box.findChildren<QLabel *>()) {
            label->setOpenExternalLinks(false);
            connect(label, &QLabel::linkActivated, &box, [&box](const QString &link) {
                const QUrl url(link);
                if (!url.isLocalFile()) {
                    QDesktopServices::openUrl(url);
                    return;
                }
                QFile file(url.toLocalFile());
                if (!file.open(QIODevice::ReadOnly)) {
                    QMessageBox::warning(&box, uiText(QStringLiteral("Licence")),
                                         uiText(QStringLiteral("Licenční soubor nelze otevřít.")));
                    return;
                }
                QDialog dialog(&box);
                dialog.setWindowTitle(uiText(QStringLiteral("Licence")));
                dialog.resize(700, 520);
                auto *layout = new QVBoxLayout(&dialog);
                auto *text = new QTextBrowser(&dialog);
                text->setOpenExternalLinks(true);
                const auto contents = QString::fromUtf8(file.readAll());
                if (file.fileName().endsWith(".md"))
                    text->setMarkdown(contents);
                else
                    text->setPlainText(contents);
                layout->addWidget(text);
                auto *buttons = new QDialogButtonBox(&dialog);
                buttons->addButton(uiText(QStringLiteral("Zavřít")), QDialogButtonBox::RejectRole);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                layout->addWidget(buttons);
                dialog.exec();
            });
        }
        box.exec();
    });
    menuBar()->addMenu(uiText(QStringLiteral("Nápověda")))->addAction(aboutAction);
    for (auto *a : documentActions)
        a->setEnabled(false);
    page->setEnabled(false);
    connect(view, &PdfView::pageChanged, this, [this, documentActions](int current, int total) {
        if (total == 0) {
            pendingFolderRequest = 0;
            cancelAreaOperation();
        }
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
        if (automaticOpening) {
            view->suppressNavigationInput();
            automaticOpening = false;
        }
        openedPath = p;
        lastFolder = QFileInfo(p).absolutePath();
        setWindowTitle(QFileInfo(p).fileName() + QStringLiteral(" — Airlet PDF"));
        statusBar()->showMessage(uiText(QStringLiteral(
            "Otočení mění pouze zobrazení. Ctrl+kolečko: zoom; prostřední tlačítko: posun.")));
    });
    connect(view, &PdfView::openFailed, this, [this](QString p, OpenResult result) {
        if (result.error == OpenError::Password) {
            bool ok = false;
            const auto password = QInputDialog::getText(
                this, uiText(QStringLiteral("Chráněné PDF")),
                uiText(QStringLiteral("Heslo dokumentu:")), QLineEdit::Password, {}, &ok);
            if (ok)
                view->openFile(p, password, openPrevious);
        } else
            QMessageBox::warning(this, QStringLiteral("Airlet PDF"), result.message);
    });
    connect(view, &PdfView::firstImage, this, [this](qint64 ms) {
        if (QCoreApplication::arguments().contains("--metrics"))
            qInfo("first_image_from_open_ms=%lld", static_cast<long long>(ms));
        statusBar()->showMessage(
            uiText(QStringLiteral(
                       "První obraz za %1 ms · Ctrl+kolečko: zoom · prostřední tlačítko: posun"))
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
                menu.addAction(uiText(QStringLiteral("Vybrat text")), textMode, &QAction::trigger);
                menu.addAction(uiText(QStringLiteral("Vybrat výřez")), areaMode, &QAction::trigger);
                menu.addAction(uiText(QStringLiteral("Zrušit výběr")), view,
                               &PdfView::clearSelection);
                menu.addSeparator();
                menu.addAction(uiText(QStringLiteral("Šířka stránky")), view, &PdfView::fitWidth);
                menu.addAction(uiText(QStringLiteral("Celá stránka")), view, &PdfView::fitPage);
                menu.addAction(uiText(QStringLiteral("Otočit doprava")), this,
                               [this] { view->rotate(1); });
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
        statusBar()->showMessage(uiText(QStringLiteral("Text je ve schránce.")));
        return;
    }
    if (!view->hasAreaSelection())
        return;
    if (imageSaver.isRunning() || pendingImage) {
        statusBar()->showMessage(
            uiText(QStringLiteral("Nejprve dokončete nebo zrušte předchozí výřez.")));
        return;
    }
    pendingSavePath.clear();
    beginAreaOperation();
}
void MainWindow::saveArea() {
    if (!view->hasAreaSelection())
        return;
    if (imageSaver.isRunning() || pendingImage) {
        statusBar()->showMessage(
            uiText(QStringLiteral("Nejprve dokončete nebo zrušte předchozí výřez.")));
        return;
    }
    QString filter = QStringLiteral("PNG (*.png)");
    const auto name = QFileInfo(openedPath).completeBaseName() + QStringLiteral("_vyrez.png");
    auto target = QFileDialog::getSaveFileName(this, uiText(QStringLiteral("Uložit výřez")),
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
            this, uiText(QStringLiteral("Výřez")),
            uiText(QStringLiteral("Zvolte soubor PNG nebo JPG. PDF dokument se nepřepisuje.")));
        return;
    }
    const bool exists = QFileInfo::exists(target);
    if (exists && QMessageBox::question(
                      this, uiText(QStringLiteral("Přepsat obrázek?")),
                      uiText(QStringLiteral("Soubor %1 už existuje. Chcete jej přepsat?"))
                          .arg(QFileInfo(target).fileName()),
                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
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
    cropProgress = new QProgressDialog(uiText(QStringLiteral("Vytvářím výřez…")),
                                       uiText(QStringLiteral("Zrušit")), 0, 100, this);
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
    statusBar()->showMessage(uiText(QStringLiteral("Požadavek na výřez byl zrušen.")));
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
    updateProcess.disconnect(this);
    if (updateProcess.state() != QProcess::NotRunning) {
        updateProcess.kill();
        updateProcess.waitForFinished(3000);
    }
    updateTemporary.reset();
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
        statusBar()->showMessage(uiText(QStringLiteral("Export už probíhá.")));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(uiText(QStringLiteral("Export stránek do obrázků")));
    QFormLayout form(&dialog);
    QComboBox scope;
    scope.addItems({uiText(QStringLiteral("Aktuální stránka")),
                    uiText(QStringLiteral("Všechny stránky")),
                    uiText(QStringLiteral("Vlastní rozsah"))});
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
    form.addRow(uiText(QStringLiteral("Stránky:")), &scope);
    form.addRow(uiText(QStringLiteral("Rozsah:")), &range);
    form.addRow(uiText(QStringLiteral("Formát:")), &format);
    form.addRow(QStringLiteral("DPI:"), &dpi);
    form.addRow(uiText(QStringLiteral("Kvalita JPG:")), &quality);
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
        QMessageBox::warning(this, uiText(QStringLiteral("Rozsah")), error);
        return;
    }
    const auto parent = QFileDialog::getExistingDirectory(
        this, uiText(QStringLiteral("Nadřazená složka exportu (vytvoří se složka podle PDF)")),
        lastFolder);
    if (parent.isEmpty())
        return;
    const auto output = QDir(parent).filePath(QFileInfo(openedPath).completeBaseName());
    if (!QDir().mkpath(output)) {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             uiText(QStringLiteral("Složku nelze vytvořit.")));
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
    const QJsonObject job{{"language", languageCode()},
                          {"source", openedPath},
                          {"password", view->exportPassword()},
                          {"range", specification},
                          {"dpi", dpi.value()},
                          {"quality", quality.value()},
                          {"format", format.currentText()},
                          {"output", output},
                          {"temporary", exportTemporary->path()},
                          {"rotations", rotations}};
    exportCompleted = 0;
    exportError.clear();
    exportCancelled = false;
    exportBuffer.clear();
    exportProgress = new QProgressDialog(uiText(QStringLiteral("Exportuji stránky…")),
                                         uiText(QStringLiteral("Zrušit")), 0, pages.size(), this);
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
    connect(&exportProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, output](int code, QProcess::ExitStatus status) {
                if (exportProgress) {
                    exportProgress->hide();
                    exportProgress->deleteLater();
                    exportProgress = nullptr;
                }
                exportTemporary.reset();
                const auto message = uiText(QStringLiteral("Dokončeno %1 stran. Složka: %2"))
                                         .arg(exportCompleted)
                                         .arg(output);
                if (exportCancelled)
                    QMessageBox::information(
                        this, uiText(QStringLiteral("Export zrušen")),
                        message + uiText(QStringLiteral("\nHotové obrázky zůstaly zachovány, "
                                                        "nedokončený výstup byl odstraněn.")));
                else if (code != 0 || status != QProcess::NormalExit)
                    QMessageBox::warning(
                        this, uiText(QStringLiteral("Export nebyl dokončen")),
                        message + "\n" +
                            (exportError.isEmpty()
                                 ? uiText(QStringLiteral("Pomocný proces skončil chybou."))
                                 : exportError));
                else
                    QMessageBox::information(this, uiText(QStringLiteral("Export dokončen")),
                                             message);
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
    const auto p = QFileDialog::getOpenFileName(
        this, uiText(QStringLiteral("Otevřít PDF")), lastFolder,
        uiText(QStringLiteral("PDF dokumenty (*.pdf);;Všechny soubory (*)")));
    if (!p.isEmpty())
        openFile(p);
}
void MainWindow::openFile(const QString &p) {
    openDocument(p, false);
}
void MainWindow::openDocument(const QString &p, bool startAtEnd) {
    if (!p.isEmpty()) {
        pendingFolderRequest = 0;
        automaticOpening = false;
        openedPath.clear();
        setWindowTitle(QStringLiteral("Airlet PDF"));
        openPrevious = startAtEnd;
        view->openFile(QFileInfo(p).absoluteFilePath(), {}, startAtEnd);
    }
}
void MainWindow::closeEvent(QCloseEvent *e) {
    updateProcess.disconnect(this);
    if (updateProcess.state() != QProcess::NotRunning) {
        updateProcess.kill();
        updateProcess.waitForFinished(3000);
    }
    updateTemporary.reset();
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
