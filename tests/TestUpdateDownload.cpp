#include "update/UpdateDialog.h"
#include "update/UpdateDownload.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <deque>
using namespace airlet;
namespace {
struct Response {
    int status = 200;
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QUrl redirect;
    bool stall = false;
};
class Reply final : public QNetworkReply {
    Response response;
    QByteArray body;
    qint64 position = 0;
    void finish() {
        if (isFinished())
            return;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        body = response.body;
        position = 0;
        emit readyRead();
        if (response.stall || isFinished())
            return;
        if (response.error != NoError)
            setError(response.error, "synthetic failure");
        setFinished(true);
        emit finished();
    }

  public:
    Reply(QNetworkRequest request, Response r, QObject *parent)
        : QNetworkReply(parent), response(r) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly);
        connect(this, &QNetworkReply::redirectAllowed, this, [this] {
            setUrl(response.redirect);
            QTimer::singleShot(0, this, [this] { finish(); });
        });
        QTimer::singleShot(0, this, [this] {
            if (!response.redirect.isEmpty()) {
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 302);
                body = "redirect response body must not enter the asset";
                emit readyRead();
                emit redirected(response.redirect);
            } else
                finish();
        });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return body.size() - position + QIODevice::bytesAvailable();
    }
    bool isSequential() const override {
        return true;
    }

  protected:
    qint64 readData(char *out, qint64 max) override {
        const auto n = std::min(max, body.size() - position);
        if (n <= 0)
            return isFinished() ? -1 : 0;
        std::memcpy(out, body.constData() + position, size_t(n));
        position += n;
        return n;
    }
};
class Network final : public QNetworkAccessManager {
  public:
    std::deque<Response> responses;
    QList<QNetworkRequest> requests;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.push_back(request);
        Response next{503, "unplanned request", QNetworkReply::ServiceUnavailableError};
        if (!responses.empty()) {
            next = responses.front();
            responses.pop_front();
        }
        return new Reply(request, next, this);
    }
};
const QUrl
    primary("https://github.com/zafodtom/airlet-pdf/releases/download/v1.0.0/airlet-update.json");
const QUrl fallback("https://api.github.com/repos/zafodtom/airlet-pdf/releases/assets/123");
QJsonObject releaseMetadata(const QByteArray &payload, bool withDigest) {
    const QString prefix = "https://github.com/zafodtom/airlet-pdf/releases/download/v1.0.0/";
    QJsonObject installer{{"name", "AirletPDF-Setup-1.0.0-x64.exe"},
                          {"browser_download_url", prefix + "AirletPDF-Setup-1.0.0-x64.exe"},
                          {"size", payload.size()}};
    if (withDigest)
        installer["digest"] =
            "sha256:" + QString::fromLatin1(
                            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    return {{"tag_name", "v1.0.0"},
            {"draft", false},
            {"prerelease", false},
            {"assets",
             QJsonArray{installer, QJsonObject{{"name", "airlet-update.json"},
                                               {"browser_download_url", primary.toString()}}}}};
}
QByteArray contents(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
} // namespace
class TestUpdateDownload final : public QObject {
    Q_OBJECT
  private slots:
    void dialogUsesDigestAndVerifiesInstaller_data() {
        QTest::addColumn<bool>("corrupt");
        QTest::newRow("verified-installer") << false;
        QTest::newRow("wrong-hash-is-rejected") << true;
    }
    void dialogUsesDigestAndVerifiesInstaller() {
        QFETCH(bool, corrupt);
        const QByteArray payload("installer test bytes");
        auto actual = payload;
        if (corrupt)
            actual[0] = 'x';
        Network network;
        network.responses = {{200, QJsonDocument(releaseMetadata(payload, true)).toJson()},
                             {200, actual}};
        QTemporaryDir dir;
        UpdateDialog dialog({{"version", "0.9.0"}, {"folder", dir.path()}}, &network);
        dialog.show();
        auto *button = dialog.findChild<QPushButton *>("downloadUpdate");
        auto *status = dialog.findChild<QLabel *>("updateStatus");
        QVERIFY(button && status);
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(network.requests.size(), 1); // No manifest fetch or dependency.
        QCOMPARE(network.requests[0].url().path(),
                 QString("/repos/zafodtom/airlet-pdf/releases/latest"));
        QTest::mouseClick(button, Qt::LeftButton);
        const auto path = dir.filePath("AirletPDF-Setup-1.0.0-x64.exe");
        if (corrupt) {
            QTRY_VERIFY(status->text().contains(QString::fromUtf8("součet")));
            QVERIFY(!button->isEnabled());
            QVERIFY(!QFile::exists(path));
        } else {
            QTRY_COMPARE(button->text(), QString::fromUtf8("Restartovat a aktualizovat"));
            QVERIFY(button->isEnabled());
            QCOMPARE(contents(path), payload);
        }
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(network.requests[1].url().path().endsWith("AirletPDF-Setup-1.0.0-x64.exe"));
        // The install/confirmation button is deliberately not clicked.
    }
    void dialogRetainsLegacyManifest() {
        const QByteArray payload("installer test bytes");
        const auto hash = QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        const QJsonObject manifest{
            {"schema", 1},
            {"version", "1.0.0"},
            {"installer", QJsonObject{{"name", "AirletPDF-Setup-1.0.0-x64.exe"},
                                      {"size", payload.size()},
                                      {"sha256", hash}}}};
        Network network;
        network.responses = {{200, QJsonDocument(releaseMetadata(payload, false)).toJson()},
                             {200, QJsonDocument(manifest).toJson()}};
        QTemporaryDir dir;
        UpdateDialog dialog({{"version", "0.9.0"}, {"folder", dir.path()}}, &network);
        auto *button = dialog.findChild<QPushButton *>("downloadUpdate");
        QVERIFY(button);
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[1].url(), primary);
    }
    void manifestFallback() {
        Network network;
        network.responses = {{504, "gateway error", QNetworkReply::UnknownServerError},
                             {200, "manifest"}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy done(&transfer, &UpdateDownload::completed),
            failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(done[0][0].toByteArray(), QByteArray("manifest"));
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[1].url(), fallback);
        QCOMPARE(network.requests[1].rawHeader("Accept"), QByteArray("application/octet-stream"));
        QCOMPARE(network.requests[1].attribute(QNetworkRequest::CacheLoadControlAttribute).toInt(),
                 int(QNetworkRequest::AlwaysNetwork));
    }
    void unavailableDirectRouteFallsBack_data() {
        QTest::addColumn<int>("status");
        QTest::newRow("forbidden-direct-route") << 403;
        QTest::newRow("not-found-direct-route") << 404;
    }
    void unavailableDirectRouteFallsBack() {
        QFETCH(int, status);
        Network network;
        network.responses = {{status, "unavailable route", QNetworkReply::ContentAccessDenied},
                             {200, "manifest"}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy done(&transfer, &UpdateDownload::completed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[1].url(), fallback);
    }
    void apiForbiddenIsNotRepeated() {
        Network network;
        network.responses = {{504, {}, QNetworkReply::UnknownServerError},
                             {403, {}, QNetworkReply::ContentAccessDenied}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(failed[0][0].toString().contains("HTTP 403"));
    }
    void metadataRetryUsesJson() {
        Network network;
        network.responses = {{503, "unavailable", QNetworkReply::ServiceUnavailableError},
                             {200, "{}"}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy done(&transfer, &UpdateDownload::completed);
        transfer.start(QUrl("https://api.github.com/repos/zafodtom/airlet-pdf/releases/latest"), {},
                       100);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        for (const auto &r : network.requests)
            QCOMPARE(r.rawHeader("Accept"), QByteArray("application/vnd.github+json"));
    }
    void partialInstallerRestartsCleanly() {
        QTemporaryDir dir;
        const auto path = dir.filePath("setup.exe");
        const QByteArray payload("complete\0installer", 18);
        Network network;
        network.responses = {{200, "partial", QNetworkReply::RemoteHostClosedError},
                             {200, payload}};
        {
            UpdateDownload transfer(nullptr, &network);
            QSignalSpy done(&transfer, &UpdateDownload::completed);
            transfer.start(primary, fallback, payload.size(), path);
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(contents(path), payload);
            QCOMPARE(done[0][1].toLongLong(), qint64(payload.size()));
            QCOMPARE(done[0][2].toString(),
                     QString::fromLatin1(
                         QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
        }
        QCOMPARE(contents(path), payload); // Successful file belongs to caller.
    }
    void redirectBodyExcluded() {
        Network network;
        network.responses = {{200, "only the asset", QNetworkReply::NoError,
                              QUrl("https://release-assets.githubusercontent.com/signed/asset")}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy done(&transfer, &UpdateDownload::completed);
        transfer.start(primary, fallback, 20);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toByteArray(), QByteArray("only the asset"));
        QCOMPARE(done[0][1].toLongLong(), 14LL);
    }
    void blockedRedirect() {
        Network network;
        network.responses = {
            {200, "asset", QNetworkReply::NoError, QUrl("http://github.com/asset")}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(failed[0][0].toString().contains(QString::fromUtf8("nepovolenou")));
    }
    void boundedRetriesAndHttpStatus() {
        Network network;
        network.responses = {{504, {}, QNetworkReply::UnknownServerError},
                             {504, {}, QNetworkReply::UnknownServerError},
                             {504, {}, QNetworkReply::UnknownServerError}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 3);
        QVERIFY(failed[0][0].toString().contains("HTTP 504"));
    }
    void reportsActualRedirectHost() {
        Network network;
        network.responses = {{404,
                              {},
                              QNetworkReply::ContentNotFoundError,
                              QUrl("https://release-assets.githubusercontent.com/signed/asset")}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, {}, 100);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(failed[0][0].toString().contains("release-assets.githubusercontent.com"));
        QVERIFY(failed[0][0].toString().contains("HTTP 404"));
    }
    void tlsFailureIsNotRetried() {
        Network network;
        network.responses = {{0, {}, QNetworkReply::SslHandshakeFailedError}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 1);
    }
    void transferAbortRetries() {
        Network network;
        network.responses = {{0, {}, QNetworkReply::OperationCanceledError}, {200, "manifest"}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy done(&transfer, &UpdateDownload::completed);
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[1].url(), fallback);
        QCOMPARE(done[0][0].toByteArray(), QByteArray("manifest"));
    }
    void cancelDuringRetry() {
        Network network;
        network.responses = {{504, {}, QNetworkReply::UnknownServerError}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed),
            done(&transfer, &UpdateDownload::completed);
        connect(&transfer, &UpdateDownload::retrying, &transfer, &UpdateDownload::cancel);
        transfer.start(primary, fallback, 100);
        QTest::qWait(1200);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(failed.isEmpty() && done.isEmpty());
    }
    void cancelPartialFile() {
        QTemporaryDir dir;
        const auto path = dir.filePath("setup.exe");
        Network network;
        network.responses = {{200, "partial", QNetworkReply::NoError, {}, true}};
        UpdateDownload transfer(nullptr, &network);
        transfer.start(primary, fallback, 100, path);
        QTRY_VERIFY(QFile::exists(path));
        transfer.cancel();
        QVERIFY(!QFile::exists(path));
    }
    void oversizedResponse_data() {
        QTest::addColumn<bool>("toFile");
        QTest::newRow("memory") << false;
        QTest::newRow("file") << true;
    }
    void oversizedResponse() {
        QFETCH(bool, toFile);
        QTemporaryDir dir;
        const auto path = toFile ? dir.filePath("setup.exe") : QString();
        Network network;
        network.responses = {{200, QByteArray(100, 'x')}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 10, path);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(path.isEmpty() || !QFile::exists(path));
    }
    void existingFileIsPreserved() {
        QTemporaryDir dir;
        const auto path = dir.filePath("setup.exe");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("existing");
        file.close();
        Network network;
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed);
        transfer.start(primary, fallback, 100, path);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(contents(path), QByteArray("existing"));
        QCOMPARE(network.requests.size(), 0);
    }
    void restartAfterFailure() {
        Network network;
        network.responses = {{404, "not found", QNetworkReply::ContentNotFoundError},
                             {200, "manifest"}};
        UpdateDownload transfer(nullptr, &network);
        QSignalSpy failed(&transfer, &UpdateDownload::failed),
            done(&transfer, &UpdateDownload::completed);
        transfer.start(primary, {}, 100);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(failed[0][0].toString().contains("HTTP 404"));
        transfer.start(primary, fallback, 100);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toByteArray(), QByteArray("manifest"));
    }
};
QTEST_MAIN(TestUpdateDownload)
#include "TestUpdateDownload.moc"
