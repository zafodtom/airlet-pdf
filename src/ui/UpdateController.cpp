#include "DocumentTools.h"
#include "MainWindow.h"
#include "i18n/Language.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
namespace airlet {
void MainWindow::checkUpdates() {
#ifndef Q_OS_WIN
    QMessageBox::information(
        this, uiText(QStringLiteral("Aktualizace")),
        uiText(QStringLiteral("Integrovaná instalace aktualizací je určena pro Windows.")));
    return;
#else
    if (updateProcess.state() != QProcess::NotRunning)
        return;
    if (imageSaver.isRunning() || pendingImage || exportProcess.state() != QProcess::NotRunning ||
        tools->isBusy()) {
        QMessageBox::information(
            this, uiText(QStringLiteral("Aktualizace")),
            uiText(QStringLiteral("Nejprve dokončete export, tisk nebo tvorbu PDF.")));
        return;
    }
    const QDir app(QCoreApplication::applicationDirPath());
    if (app.entryList({"unins*.exe"}, QDir::Files).isEmpty()) {
        QMessageBox::information(
            this, uiText(QStringLiteral("Aktualizace")),
            uiText(QStringLiteral(
                "Integrovaná aktualizace je určena pro nainstalovanou verzi. Přenosnou verzi "
                "aktualizujte rozbalením nového přenosného balíčku.")));
        return;
    }
    // Only completed, old handoffs: never touch another in-progress download.
    const QDir temporary(QDir::tempPath());
    for (const auto &name : temporary.entryList(
             {"AirletPDF-update-*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto folder = temporary.filePath(name);
        const QFileInfo done(QDir(folder).filePath("completed"));
        if (done.exists() && done.lastModified().secsTo(QDateTime::currentDateTime()) > 300)
            QDir(folder).removeRecursively();
    }
    updateTemporary =
        std::make_unique<QTemporaryDir>(temporary.filePath("AirletPDF-update-XXXXXX"));
    if (!updateTemporary->isValid()) {
        updateTemporary.reset();
        return;
    }
    updateBuffer.clear();
    updateProcess.disconnect(this);
    connect(&updateProcess, &QProcess::readyReadStandardOutput, this, [this] {
        updateBuffer += updateProcess.readAllStandardOutput();
        if (updateBuffer.size() > 16384)
            updateProcess.kill();
    });
    connect(&updateProcess, &QProcess::readyReadStandardError, this,
            [this] { updateProcess.readAllStandardError(); });
    connect(&updateProcess, &QProcess::started, this, [this] {
        const QJsonObject job{{"version", QCoreApplication::applicationVersion()},
                              {"folder", updateTemporary->path()},
                              {"language", languageCode()}};
        updateProcess.write(QJsonDocument(job).toJson(QJsonDocument::Compact) + '\n');
        updateProcess.closeWriteChannel();
    });
    connect(&updateProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        setEnabled(true);
        updateTemporary.reset();
        QMessageBox::warning(this, uiText(QStringLiteral("Aktualizace")),
                             updateProcess.errorString());
    });
    connect(&updateProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                updateBuffer += updateProcess.readAllStandardOutput();
                setEnabled(true);
                if (code != 0 || status != QProcess::NormalExit ||
                    updateBuffer.trimmed().isEmpty()) {
                    updateTemporary.reset();
                    return;
                }
                const auto result = QJsonDocument::fromJson(updateBuffer.trimmed()).object();
                const auto name = result["installer"].toString(), sha = result["sha256"].toString();
                if (!QRegularExpression("^AirletPDF-Setup-[0-9]+\\.[0-9]+\\.[0-9]+-x64\\.exe$")
                         .match(name)
                         .hasMatch() ||
                    !QRegularExpression("^[0-9a-f]{64}$").match(sha).hasMatch()) {
                    updateTemporary.reset();
                    return;
                }
                const auto helper = QDir(updateTemporary->path()).filePath("ApplyUpdate.exe");
                const auto installer = QDir(updateTemporary->path()).filePath(name);
                const bool copied = QFile::copy(
                    QDir(QCoreApplication::applicationDirPath()).filePath("AirletApplyUpdate.exe"),
                    helper);
                const QStringList args{
                    QString::number(QCoreApplication::applicationPid()),
                    QDir::toNativeSeparators(installer),
                    QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
                    sha,
                    languageCode(),
                    QDir::toNativeSeparators(openedPath)};
                if (!copied || !QProcess::startDetached(helper, args)) {
                    updateTemporary.reset();
                    QMessageBox::warning(
                        this, uiText(QStringLiteral("Aktualizace")),
                        uiText(QStringLiteral(
                            "Aktualizaci se nepodařilo spustit. Aplikace zůstává otevřená.")));
                    return;
                }
                updateTemporary->setAutoRemove(false);
                updateTemporary.reset();
                close();
            });
    setEnabled(false);
    updateProcess.start(app.filePath("AirletUpdate.exe"), {});
#endif
}
} // namespace airlet
