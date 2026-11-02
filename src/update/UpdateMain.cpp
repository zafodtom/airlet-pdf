#include "UpdateProtocol.h"
#include "i18n/Language.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <functional>
using namespace airlet;
class UpdateDialog final : public QDialog {
    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QFile file;
    QLabel *status;
    QProgressBar *progress;
    QPushButton *check, *download;
    QString version, folder, failure;
    UpdateRelease release;
    QByteArray buffer;
    qint64 received = 0, limit = 0;
    bool binary = false, ready = false;
    void fail(QString error) {
        status->setText(error);
        check->setEnabled(true);
        download->setEnabled(false);
        progress->setRange(0, 100);
        progress->setValue(0);
        if (file.isOpen())
            file.close();
        if (!file.fileName().isEmpty())
            file.remove();
    }
    void drain() {
        if (!reply || !failure.isEmpty())
            return;
        const auto bytes = reply->readAll();
        received += bytes.size();
        if (received > limit) {
            failure = uiText(QStringLiteral("Aktualizační data překročila povolenou velikost."));
            reply->abort();
            return;
        }
        if (binary) {
            if (file.write(bytes) != bytes.size()) {
                failure = file.errorString();
                reply->abort();
                return;
            }
            hash.addData(bytes);
            progress->setValue(int(received * 100 / release.size));
        } else
            buffer += bytes;
    }
    void fetch(QUrl url, qint64 maximum, bool toFile, std::function<void()> completed) {
        check->setEnabled(false);
        download->setEnabled(false);
        failure.clear();
        buffer.clear();
        received = 0;
        limit = maximum;
        binary = toFile;
        if (toFile) {
            file.setFileName(QDir(folder).filePath(release.installerName));
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                fail(file.errorString());
                return;
            }
            hash.reset();
            progress->setRange(0, 100);
            progress->setValue(0);
        } else
            progress->setRange(0, 0);
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "AirletPDF-ManualUpdater");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        request.setRawHeader("Accept", url.host() == "api.github.com"
                                           ? "application/vnd.github+json"
                                           : "application/octet-stream");
        request.setTransferTimeout(30000);
        request.setMaximumRedirectsAllowed(8);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::UserVerifiedRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        reply = network.get(request);
        auto *active = reply.data();
        active->setReadBufferSize(256 * 1024);
        connect(active, &QNetworkReply::redirected, this, [this, active](const QUrl &url) {
            if (allowedUpdateUrl(url))
                active->redirectAllowed();
            else {
                failure =
                    uiText(QStringLiteral("Aktualizace byla přesměrována na nepovolenou adresu."));
                active->abort();
            }
        });
        connect(active, &QIODevice::readyRead, this, [this] { drain(); });
        connect(active, &QNetworkReply::finished, this, [this, active, completed] {
            drain();
            const auto error = active->error();
            const int code = active->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto detail = active->errorString();
            if (file.isOpen()) {
                if (binary && !file.flush())
                    failure = file.errorString();
                file.close();
            }
            reply = nullptr;
            active->deleteLater();
            if (!failure.isEmpty() || error != QNetworkReply::NoError || code != 200) {
                fail(!failure.isEmpty() ? failure
                     : code == 404
                         ? uiText(QStringLiteral(
                               "Zatím není zveřejněno stabilní vydání s aktualizačním balíčkem."))
                         : uiText(QStringLiteral("Kontrola nebo stažení selhalo: %1")).arg(detail));
                return;
            }
            completed();
        });
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
                  fetch(release.manifestUrl, 65536, false, [this] {
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
                  });
              });
    }

  public:
    UpdateDialog(const QJsonObject &job)
        : version(job["version"].toString()), folder(job["folder"].toString()) {
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
            fetch(release.installerUrl, release.size, true, [this] {
                if (received != release.size ||
                    QString::fromLatin1(hash.result().toHex()) != release.sha256) {
                    fail(uiText(
                        QStringLiteral("Kontrolní součet staženého instalátoru nesouhlasí.")));
                    return;
                }
                ready = true;
                status->setText(uiText(QStringLiteral("Aktualizace je stažená a ověřená.")));
                download->setText(uiText(QStringLiteral("Restartovat a aktualizovat")));
                download->setEnabled(true);
                progress->setValue(100);
            });
        });
        QTimer::singleShot(0, this, [this] { startCheck(); });
    }
    ~UpdateDialog() override {
        if (reply) {
            reply->disconnect(this);
            reply->abort();
        }
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
