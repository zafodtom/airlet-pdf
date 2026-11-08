#include "UpdateDownload.h"
#include "UpdateProtocol.h"
#include "i18n/Language.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <functional>
using namespace airlet;
class UpdateDialog final : public QDialog {
    UpdateDownload transfer;
    std::function<void()> completion;
    QLabel *status;
    QProgressBar *progress;
    QPushButton *check, *download;
    QString version, folder;
    UpdateRelease release;
    QByteArray buffer;
    qint64 received = 0;
    QString downloadedHash;
    bool binary = false, ready = false;
    void fail(QString error) {
        status->setText(error);
        check->setEnabled(true);
        download->setEnabled(false);
        progress->setRange(0, 100);
        progress->setValue(0);
    }
    void fetch(QUrl url, qint64 maximum, bool toFile, std::function<void()> completed,
               QUrl fallback = {}) {
        check->setEnabled(false);
        download->setEnabled(false);
        buffer.clear();
        received = 0;
        binary = toFile;
        completion = std::move(completed);
        progress->setRange(0, toFile ? 100 : 0);
        progress->setValue(0);
        transfer.start(url, fallback, maximum,
                       toFile ? QDir(folder).filePath(release.installerName) : QString());
    }
    void startCheck() {
        ready = false;
        download->setText(uiText(QStringLiteral("Stáhnout aktualizaci")));
        status->setText(uiText(QStringLiteral("Kontroluji nové stabilní vydání…")));
        fetch(QUrl("https://api.github.com/repos/zafodtom/airlet-pdf/releases/latest"),
              2 * 1024 * 1024, false, [this] {
                  QString error;
                  const auto state = parseUpdateRelease(QJsonDocument::fromJson(buffer).object(),
                                                        version, &release, &error);
                  if (state == ReleaseState::Invalid) {
                      fail(error);
                      return;
                  }
                  if (state == ReleaseState::Current) {
                      status->setText(uiText(QStringLiteral("Používáte aktuální verzi.")));
                      progress->setRange(0, 100);
                      progress->setValue(100);
                      check->setEnabled(true);
                      return;
                  }
                  fetch(
                      release.manifestUrl, 65536, false,
                      [this] {
                          QString error;
                          if (!validateUpdateManifest(QJsonDocument::fromJson(buffer).object(),
                                                      &release, &error)) {
                              fail(error);
                              return;
                          }
                          status->setText(uiText(QStringLiteral("Je dostupná verze %1 (%2 MB)."))
                                              .arg(release.version)
                                              .arg(release.size / (1024 * 1024)));
                          progress->setRange(0, 100);
                          progress->setValue(0);
                          download->setEnabled(true);
                      },
                      release.manifestApiUrl);
              });
    }

  public:
    UpdateDialog(const QJsonObject &job)
        : version(job["version"].toString()), folder(job["folder"].toString()) {
        connect(&transfer, &UpdateDownload::failed, this, [this](QString error) { fail(error); });
        connect(&transfer, &UpdateDownload::progress, this, [this](qint64 bytes) {
            if (binary && release.size > 0)
                progress->setValue(int(bytes * 100 / release.size));
        });
        connect(&transfer, &UpdateDownload::retrying, this, [this] {
            status->setText(uiText(QStringLiteral("Obnovuji přerušené stahování…")));
        });
        connect(&transfer, &UpdateDownload::completed, this,
                [this](QByteArray data, qint64 bytes, QString sha) {
                    buffer = std::move(data);
                    received = bytes;
                    downloadedHash = sha;
                    auto next = std::move(completion);
                    if (next)
                        next();
                });
        setWindowTitle(uiText(QStringLiteral("Aktualizace Airlet PDF")));
        setWindowIcon(QIcon(":/icons/airlet.png"));
        resize(540, 200);
        auto *layout = new QVBoxLayout(this);
        auto *current =
            new QLabel(uiText(QStringLiteral("Nainstalovaná verze: %1")).arg(version), this);
        layout->addWidget(current);
        status = new QLabel(this);
        status->setWordWrap(true);
        layout->addWidget(status);
        progress = new QProgressBar(this);
        layout->addWidget(progress);
        check = new QPushButton(uiText(QStringLiteral("Zkontrolovat znovu")), this);
        layout->addWidget(check);
        download = new QPushButton(uiText(QStringLiteral("Stáhnout aktualizaci")), this);
        download->setEnabled(false);
        layout->addWidget(download);
        auto *close = new QPushButton(uiText(QStringLiteral("Zavřít")), this);
        layout->addWidget(close);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        connect(check, &QPushButton::clicked, this, [this] { startCheck(); });
        connect(download, &QPushButton::clicked, this, [this] {
            if (ready) {
                if (QMessageBox::question(
                        this, uiText(QStringLiteral("Nainstalovat aktualizaci")),
                        uiText(QStringLiteral(
                            "Airlet PDF se zavře, aktualizuje a znovu otevře. Pokračovat?"))) !=
                    QMessageBox::Yes)
                    return;
                const auto bytes = QJsonDocument(QJsonObject{{"installer", release.installerName},
                                                             {"sha256", release.sha256}})
                                       .toJson(QJsonDocument::Compact) +
                                   '\n';
                std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
                std::fflush(stdout);
                accept();
                return;
            }
            status->setText(uiText(QStringLiteral("Stahuji aktualizaci…")));
            fetch(
                release.installerUrl, release.size, true,
                [this] {
                    if (received != release.size || downloadedHash != release.sha256) {
                        QFile::remove(QDir(folder).filePath(release.installerName));
                        fail(uiText(
                            QStringLiteral("Kontrolní součet staženého instalátoru nesouhlasí.")));
                        return;
                    }
                    ready = true;
                    status->setText(uiText(QStringLiteral("Aktualizace je stažená a ověřená.")));
                    download->setText(uiText(QStringLiteral("Restartovat a aktualizovat")));
                    download->setEnabled(true);
                    progress->setValue(100);
                },
                release.installerApiUrl);
        });
        QTimer::singleShot(0, this, [this] { startCheck(); });
    }
    ~UpdateDialog() override {
        transfer.cancel();
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    const auto job = QJsonDocument::fromJson(input.readLine()).object();
    if (job["folder"].toString().isEmpty() || !QDir(job["folder"].toString()).exists())
        return 1;
    setLanguage(job["language"].toString());
    UpdateDialog dialog(job);
    dialog.show();
    return app.exec();
}
