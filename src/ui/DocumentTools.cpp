#include "DocumentTools.h"
#include "MainWindow.h"
#include "PdfView.h"
#include "i18n/Language.h"
#include <QAction>
#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>

namespace airlet {
namespace {
class Task final : public QObject {
  public:
    QProcess process;
    QByteArray buffer;
    QString error;
    bool success = false, cancelled = false;
    std::unique_ptr<QTemporaryDir> temporary;
    QPointer<QProgressDialog> progress;
    std::function<void(QJsonObject)> record;
    std::function<void(bool, QString)> done;
    Task(QObject *parent) : QObject(parent) {
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] { drain(); });
        connect(&process, &QProcess::readyReadStandardError, this,
                [this] { process.readAllStandardError(); });
        connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
                    drain();
                    if (progress) {
                        progress->hide();
                        progress->deleteLater();
                    }
                    temporary.reset();
                    if (done)
                        done(!cancelled && code == 0 && status == QProcess::NormalExit && success,
                             cancelled ? uiText(QStringLiteral("Operace byla zrušena."))
                             : error.isEmpty()
                                 ? uiText(QStringLiteral("Pomocný proces nedokončil operaci."))
                                 : error);
                    deleteLater();
                });
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            if (progress) {
                progress->hide();
                progress->deleteLater();
            }
            temporary.reset();
            if (done)
                done(false, process.errorString());
            deleteLater();
        });
    }
    ~Task() override {
        process.disconnect(this);
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(3000);
        }
        if (progress)
            delete progress;
    }
    void drain() {
        buffer += process.readAllStandardOutput();
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const auto o = QJsonDocument::fromJson(buffer.left(end)).object();
            buffer.remove(0, end + 1);
            if (o.contains("error"))
                error = o["error"].toString();
            if (o["success"].toBool())
                success = true;
            if (progress && o.contains("completed")) {
                progress->setMaximum(o["total"].toInt());
                progress->setValue(o["completed"].toInt());
            }
            if (record)
                record(o);
        }
    }
    void start(QJsonObject job) {
        job["language"] = languageCode();
        connect(&process, &QProcess::started, this, [this, job] {
            process.write(QJsonDocument(job).toJson(QJsonDocument::Compact) + '\n');
            process.closeWriteChannel();
        });
#ifdef Q_OS_WIN
        const auto name = QStringLiteral("AirletExport.exe");
#else
        const auto name = QStringLiteral("AirletExport");
#endif
        process.start(QDir(QCoreApplication::applicationDirPath()).filePath(name), {});
    }
    void cancel() {
        cancelled = true;
        process.kill();
    }
};
struct Hit {
    int page;
    QVector<QRectF> rects;
    QSizeF points;
};
} // namespace
class DocumentTools::State {
  public:
    MainWindow *window;
    PdfView *view;
    QString source;
    QPointer<QDialog> searchDialog;
    QLineEdit *query = nullptr;
    QLabel *searchStatus = nullptr;
    QTimer debounce;
    QPointer<Task> searchTask, operation;
    QVector<Hit> hits;
    int hit = -1;
    bool searching = false, limited = false, noText = false, stopped = false;
    QJsonObject document(const QString &mode) const {
        QJsonArray rotations;
        for (int r : view->pageRotations())
            rotations.append(r);
        return {{"mode", mode},
                {"source", source},
                {"password", view->exportPassword()},
                {"rotations", rotations}};
    }
    void stopSearch() {
        debounce.stop();
        if (searchTask) {
            searchTask->done = {};
            searchTask->record = {};
            searchTask->cancel();
            searchTask = nullptr;
        }
        searching = false;
        hits.clear();
        hit = -1;
        view->clearSearchHit();
    }
    void updateStatus() {
        if (!searchStatus)
            return;
        searchStatus->setText(
            QStringLiteral("%1 / %2%3%4")
                .arg(hit < 0 ? 0 : hit + 1)
                .arg(hits.size())
                .arg(searching ? uiText(QStringLiteral(" · hledám…"))
                     : stopped ? uiText(QStringLiteral(" · hledání zastaveno"))
                     : limited ? uiText(QStringLiteral(" · limit 2000 nálezů, upřesněte dotaz"))
                               : QString())
                .arg(!searching && hits.isEmpty()
                         ? noText ? uiText(QStringLiteral(
                                        " · dokument nemá textovou vrstvu (bez OCR)"))
                                  : uiText(QStringLiteral(" · nenalezeno"))
                         : QString()));
    }
    void navigate(int direction) {
        if (hits.isEmpty())
            return;
        hit = (hit + direction + hits.size()) % hits.size();
        view->showSearchHit(hits[hit].page, hits[hit].rects, hits[hit].points);
        updateStatus();
    }
    void search() {
        stopSearch();
        limited = false;
        noText = false;
        stopped = false;
        const auto text = query->text().simplified();
        if (text.isEmpty() || source.isEmpty()) {
            searchStatus->clear();
            return;
        }
        searchTask = new Task(window);
        searching = true;
        updateStatus();
        searchTask->record = [this](const QJsonObject &o) {
            for (const auto &v : o["matches"].toArray()) {
                if (hits.size() >= 2000)
                    break;
                const auto m = v.toObject();
                const auto size = m["size"].toArray();
                Hit h{m["page"].toInt(),
                      {},
                      size.size() == 2 ? QSizeF(size[0].toDouble(), size[1].toDouble()) : QSizeF()};
                for (const auto &b : m["rects"].toArray()) {
                    const auto a = b.toArray();
                    if (a.size() == 4)
                        h.rects.append(QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(),
                                              a[3].toDouble()));
                }
                hits.append(std::move(h));
            }
            if (hit < 0 && !hits.isEmpty())
                navigate(1);
            if (o.contains("success")) {
                limited = o["limited"].toBool();
                noText = o["textPages"].toInt() == 0;
            }
            updateStatus();
        };
        searchTask->done = [this](bool ok, QString error) {
            searchTask = nullptr;
            searching = false;
            updateStatus();
            if (!ok && searchStatus)
                searchStatus->setText(error);
        };
        auto job = document("search");
        job["query"] = text;
        searchTask->start(job);
    }
    void run(QJsonObject job, const QString &title, const QString &target = {}) {
        if (operation) {
            QMessageBox::information(
                window, title,
                uiText(
                    QStringLiteral("Nejprve dokončete nebo zrušte předchozí tisk či tvorbu PDF.")));
            return;
        }
        operation = new Task(window);
        if (!target.isEmpty()) {
            operation->temporary = std::make_unique<QTemporaryDir>(
                QDir(QFileInfo(target).absolutePath()).filePath(".airlet-job-XXXXXX"));
            if (!operation->temporary->isValid()) {
                delete operation;
                operation = nullptr;
                QMessageBox::warning(window, title,
                                     uiText(QStringLiteral("Nelze vytvořit pracovní složku.")));
                return;
            }
            job["temporary"] = operation->temporary->path();
            job["output"] = target;
        }
        auto *task = operation.data();
        task->progress = new QProgressDialog(title, uiText(QStringLiteral("Zrušit")), 0, 0, window);
        task->progress->setWindowModality(Qt::NonModal);
        task->progress->setMinimumDuration(0);
        task->progress->setAutoClose(false);
        task->progress->setAutoReset(false);
        connect(task->progress, &QProgressDialog::canceled, task, [task] { task->cancel(); });
        task->done = [this, title, target](bool ok, QString error) {
            operation = nullptr;
            if (ok)
                QMessageBox::information(
                    window, title,
                    target.isEmpty()
                        ? uiText(QStringLiteral("Dokument byl předán tiskárně."))
                        : uiText(QStringLiteral("PDF bylo vytvořeno:\n%1")).arg(target));
            else
                QMessageBox::warning(window, title, error);
        };
        task->progress->show();
        task->start(job);
    }
};
DocumentTools::DocumentTools(MainWindow *window, PdfView *view) : QObject(window), d(new State) {
    d->window = window;
    d->view = view;
    d->debounce.setSingleShot(true);
    d->debounce.setInterval(300);
    connect(&d->debounce, &QTimer::timeout, this, [this] { d->search(); });
    connect(view, &PdfView::pageChanged, this, [this](int, int total) {
        if (!total) {
            d->source.clear();
            d->stopSearch();
            if (d->searchDialog)
                d->searchDialog->hide();
        }
    });
    connect(view, &PdfView::documentOpened, this,
            [this](QString source, int) { d->source = source; });
}
DocumentTools::~DocumentTools() {
    d->stopSearch();
    if (d->operation) {
        d->operation->done = {};
        delete d->operation;
    }
    delete d;
}
bool DocumentTools::isBusy() const {
    return bool(d->operation);
}
void DocumentTools::find() {
    if (d->source.isEmpty())
        return;
    if (!d->searchDialog) {
        auto *dialog = new QDialog(d->window);
        d->searchDialog = dialog;
        dialog->setWindowTitle(uiText(QStringLiteral("Hledat v dokumentu")));
        dialog->setObjectName("searchDialog");
        auto *form = new QVBoxLayout(dialog);
        auto *row = new QHBoxLayout;
        d->query = new QLineEdit(dialog);
        d->query->setObjectName("searchQuery");
        d->query->setMaxLength(256);
        d->query->setPlaceholderText(
            uiText(QStringLiteral("Hledaný text (bez rozlišení velikosti písmen)")));
        row->addWidget(d->query);
        auto *prev = new QPushButton(uiText(QStringLiteral("Předchozí")), dialog),
             *next = new QPushButton(uiText(QStringLiteral("Další")), dialog),
             *stop = new QPushButton(uiText(QStringLiteral("Zastavit")), dialog);
        row->addWidget(prev);
        row->addWidget(next);
        row->addWidget(stop);
        form->addLayout(row);
        d->searchStatus = new QLabel(dialog);
        d->searchStatus->setObjectName("searchStatus");
        form->addWidget(d->searchStatus);
        connect(d->query, &QLineEdit::textChanged, this, [this] {
            d->stopSearch();
            d->searchStatus->setText(QStringLiteral("…"));
            d->debounce.start();
        });
        connect(d->query, &QLineEdit::returnPressed, this, [this] {
            if (d->debounce.isActive()) {
                d->debounce.stop();
                d->search();
            } else
                d->navigate(1);
        });
        next->setAutoDefault(false);
        prev->setAutoDefault(false);
        stop->setAutoDefault(false);
        connect(next, &QPushButton::clicked, this, [this] { d->navigate(1); });
        connect(prev, &QPushButton::clicked, this, [this] { d->navigate(-1); });
        connect(stop, &QPushButton::clicked, this, [this] {
            if (d->searchTask) {
                d->searchTask->done = {};
                d->searchTask->record = {};
                d->searchTask->cancel();
                d->searchTask = nullptr;
            }
            d->searching = false;
            d->stopped = true;
            d->updateStatus();
        });
        connect(dialog, &QDialog::finished, this, [this] { d->stopSearch(); });
        dialog->resize(690, 100);
    }
    d->searchDialog->show();
    d->searchDialog->raise();
    d->query->setFocus();
    d->query->selectAll();
    if (!d->query->text().isEmpty() && d->hits.isEmpty() && !d->searching)
        d->debounce.start();
}
void DocumentTools::print() {
    if (d->source.isEmpty())
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(d->source).fileName());
    printer.setFromTo(1, d->view->pageCount());
    QPrintDialog dialog(&printer, d->window);
    dialog.setMinMax(1, d->view->pageCount());
    dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, true);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto job = d->document("print");
    auto range = QStringLiteral("1-%1").arg(d->view->pageCount());
    if (printer.printRange() == QPrinter::CurrentPage)
        range = QString::number(d->view->currentPage() + 1);
    else if (printer.printRange() == QPrinter::PageRange) {
        range = printer.pageRanges().toString();
        if (range.isEmpty())
            range = QStringLiteral("%1-%2").arg(printer.fromPage()).arg(printer.toPage());
    }
    const auto mm = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
    const auto margins = printer.pageLayout().margins(QPageLayout::Millimeter);
    job["marginsMm"] = QJsonArray{margins.left(), margins.top(), margins.right(), margins.bottom()};
    job["range"] = range;
    job["paperMm"] = QJsonArray{mm.width(), mm.height()};
    job["resolution"] = printer.resolution();
    job["copies"] = printer.copyCount();
    job["collate"] = printer.collateCopies();
    job["duplex"] = int(printer.duplex());
    job["gray"] = printer.colorMode() == QPrinter::GrayScale;
    job["printer"] = printer.printerName();
    job["reverse"] = printer.pageOrder() == QPrinter::LastPageFirst;
    job["paperSource"] = int(printer.paperSource());
    const bool pdf = printer.outputFormat() == QPrinter::PdfFormat;
    job["pdf"] = pdf;
    auto target = printer.outputFileName();
    if (pdf) {
        if (target.isEmpty())
            target = QFileDialog::getSaveFileName(d->window, uiText(QStringLiteral("Tisk do PDF")),
                                                  {}, QStringLiteral("PDF (*.pdf)"), nullptr,
                                                  QFileDialog::DontConfirmOverwrite);
        if (target.isEmpty())
            return;
        if (!target.endsWith(".pdf", Qt::CaseInsensitive))
            target += ".pdf";
        if (QFileInfo::exists(target)) {
            QMessageBox::warning(d->window, uiText(QStringLiteral("Tisk")),
                                 uiText(QStringLiteral("Soubor již existuje. Zvolte nový název.")));
            return;
        }
    }
    d->run(job, uiText(QStringLiteral("Tisk dokumentu…")), pdf ? target : QString());
}
void DocumentTools::imagesPdf() {
    auto files = QFileDialog::getOpenFileNames(
        d->window, uiText(QStringLiteral("Vybrat obrázky pro nové PDF")), {},
        uiText(QStringLiteral("Obrázky (*.png *.jpg *.jpeg *.bmp)")));
    if (files.isEmpty())
        return;
    QCollator order;
    order.setNumericMode(true);
    std::sort(files.begin(), files.end(),
              [&](const QString &a, const QString &b) { return order.compare(a, b) < 0; });
    QDialog dialog(d->window);
    dialog.setWindowTitle(uiText(QStringLiteral("PDF z obrázků")));
    dialog.resize(600, 470);
    QVBoxLayout layout(&dialog);
    QLabel hint(uiText(QStringLiteral("Jeden obrázek = jedna stránka. Pořadí upravte tažením.")));
    layout.addWidget(&hint);
    QListWidget list;
    list.setDragDropMode(QAbstractItemView::InternalMove);
    list.setSelectionMode(QAbstractItemView::SingleSelection);
    for (const auto &file : files) {
        auto *item = new QListWidgetItem(QFileInfo(file).fileName(), &list);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }
    layout.addWidget(&list);
    QHBoxLayout controls;
    QPushButton up(uiText(QStringLiteral("Nahoru"))), down(uiText(QStringLiteral("Dolů"))),
        remove(uiText(QStringLiteral("Odebrat")));
    controls.addWidget(&up);
    controls.addWidget(&down);
    controls.addWidget(&remove);
    layout.addLayout(&controls);
    auto move = [&](int delta) {
        const int row = list.currentRow(), dest = row + delta;
        if (row >= 0 && dest >= 0 && dest < list.count()) {
            auto *item = list.takeItem(row);
            list.insertItem(dest, item);
            list.setCurrentRow(dest);
        }
    };
    connect(&up, &QPushButton::clicked, &dialog, [&] { move(-1); });
    connect(&down, &QPushButton::clicked, &dialog, [&] { move(1); });
    connect(&remove, &QPushButton::clicked, &dialog,
            [&] { delete list.takeItem(list.currentRow()); });
    QFormLayout form;
    QComboBox paper;
    paper.addItems({uiText(QStringLiteral("Podle obrázku (bez okrajů)")),
                    uiText(QStringLiteral("A4 na výšku (okraje 10 mm)")),
                    uiText(QStringLiteral("A4 na šířku (okraje 10 mm)"))});
    QSpinBox dpi;
    dpi.setRange(72, 600);
    dpi.setValue(150);
    form.addRow(uiText(QStringLiteral("Velikost stránky:")), &paper);
    form.addRow(uiText(QStringLiteral("DPI pro velikost podle obrázku:")), &dpi);
    layout.addLayout(&form);
    connect(&paper, qOverload<int>(&QComboBox::currentIndexChanged), &dpi,
            [&](int i) { dpi.setEnabled(i == 0); });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !list.count())
        return;
    auto target = QFileDialog::getSaveFileName(d->window, uiText(QStringLiteral("Uložit nové PDF")),
                                               {}, QStringLiteral("PDF (*.pdf)"), nullptr,
                                               QFileDialog::DontConfirmOverwrite);
    if (target.isEmpty())
        return;
    if (!target.endsWith(".pdf", Qt::CaseInsensitive))
        target += ".pdf";
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(d->window, uiText(QStringLiteral("PDF z obrázků")),
                             uiText(QStringLiteral("Soubor již existuje. Zvolte nový název.")));
        return;
    }
    QJsonArray images;
    for (int i = 0; i < list.count(); ++i)
        images.append(list.item(i)->data(Qt::UserRole).toString());
    d->run({{"mode", "images"},
            {"images", images},
            {"dpi", dpi.value()},
            {"paper", paper.currentIndex()}},
           uiText(QStringLiteral("Vytvářím PDF z obrázků…")), target);
}
} // namespace airlet
