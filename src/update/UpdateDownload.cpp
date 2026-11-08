#include "UpdateDownload.h"
#include "UpdateProtocol.h"
#include "i18n/Language.h"
#include <QNetworkRequest>
namespace airlet {
namespace {
bool transientFailure(int code, QNetworkReply::NetworkError error) {
    if (error == QNetworkReply::SslHandshakeFailedError)
        return false;
    if (code >= 400)
        return code == 408 || code == 429 || code == 500 || code == 502 || code == 503 ||
               code == 504;
    return error == QNetworkReply::ConnectionRefusedError ||
           error == QNetworkReply::RemoteHostClosedError ||
           error == QNetworkReply::HostNotFoundError || error == QNetworkReply::TimeoutError ||
           error == QNetworkReply::TemporaryNetworkFailureError ||
           error == QNetworkReply::NetworkSessionFailedError ||
           error == QNetworkReply::ProxyTimeoutError;
}
} // namespace
UpdateDownload::UpdateDownload(QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), network(manager ? manager : new QNetworkAccessManager(this)) {
    retry.setSingleShot(true);
    connect(&retry, &QTimer::timeout, this, &UpdateDownload::begin);
}
UpdateDownload::~UpdateDownload() {
    cancel();
}
void UpdateDownload::discard() {
    file.close();
    if (ownsFile)
        file.remove();
    ownsFile = false;
}
void UpdateDownload::cancel() {
    retry.stop();
    if (reply) {
        auto *active = reply.data();
        reply = nullptr;
        active->disconnect(this);
        active->abort();
        active->deleteLater();
    }
    discard();
}
void UpdateDownload::start(QUrl primaryUrl, QUrl fallbackUrl, qint64 limit, QString path) {
    cancel();
    primary = primaryUrl;
    fallback = fallbackUrl;
    maximum = limit;
    destination = path;
    attempt = 0;
    begin();
}
void UpdateDownload::begin() {
    const auto url = attempt && !fallback.isEmpty() ? fallback : primary;
    if (!allowedUpdateUrl(url) || maximum <= 0) {
        emit failed(uiText(QStringLiteral("Neplatná adresa aktualizace.")));
        return;
    }
    failure.clear();
    buffer.clear();
    received = 0;
    hash.reset();
    if (!destination.isEmpty()) {
        file.setFileName(destination);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            emit failed(file.errorString());
            return;
        }
        ownsFile = true;
    }
    emit progress(0);
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "AirletPDF-ManualUpdater");
    if (url.host() == "api.github.com")
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // API asset endpoints return JSON metadata unless octet-stream is requested.
    const bool metadata =
        url.host() == "api.github.com" && !url.path().contains("/releases/assets/");
    request.setRawHeader("Accept",
                         metadata ? "application/vnd.github+json" : "application/octet-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(30000);
    request.setMaximumRedirectsAllowed(8);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    reply = network->get(request);
    auto *active = reply.data();
    active->setReadBufferSize(256 * 1024);
    connect(active, &QNetworkReply::redirected, this, [this, active](QUrl target) {
        if (reply != active)
            return;
        target = active->url().resolved(target);
        if (allowedUpdateUrl(target))
            active->redirectAllowed();
        else {
            failure =
                uiText(QStringLiteral("Aktualizace byla přesměrována na nepovolenou adresu."));
            active->abort();
        }
    });
    connect(active, &QIODevice::readyRead, this, [this, active] { drain(active); });
    connect(active, &QNetworkReply::finished, this, [this, active, url] {
        if (reply != active)
            return;
        drain(active);
        if (reply != active)
            return;
        const auto error = active->error();
        const int code = active->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (file.isOpen()) {
            if (!file.flush())
                failure = file.errorString();
            file.close();
        }
        reply = nullptr;
        active->deleteLater();
        if (!failure.isEmpty() || error != QNetworkReply::NoError || code != 200) {
            discard();
            // At most primary + API + one delayed API retry (or two attempts
            // without a fallback). Never retry TLS, integrity or local I/O errors.
            const bool canRetry =
                transientFailure(code, error) ||
                ((code == 403 || code == 404) && error != QNetworkReply::SslHandshakeFailedError &&
                 !fallback.isEmpty() && attempt == 0);
            if (failure.isEmpty() && canRetry && attempt < (fallback.isEmpty() ? 1 : 2)) {
                ++attempt;
                retry.start(1000);
                emit retrying();
                return;
            }
            emit failed(
                !failure.isEmpty()
                    ? failure
                    : uiText(QStringLiteral("Stažení selhalo na %1 (HTTP %2, síťová chyba %3)."))
                          .arg(url.host(), code ? QString::number(code) : QStringLiteral("—"),
                               QString::number(int(error))));
            return;
        }
        // Ownership passes to the caller; cancel/destruction must not remove a
        // successfully verified installer before the native helper opens it.
        ownsFile = false;
        emit completed(buffer, received, QString::fromLatin1(hash.result().toHex()));
    });
}
void UpdateDownload::drain(QNetworkReply *active) {
    if (reply != active || !failure.isEmpty())
        return;
    const auto bytes = active->readAll();
    // Redirect and error response bodies are not part of the requested asset.
    if (active->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        return;
    received += bytes.size();
    if (received > maximum) {
        failure = uiText(QStringLiteral("Aktualizační data překročila povolenou velikost."));
        active->abort();
        return;
    }
    if (destination.isEmpty())
        buffer += bytes;
    else {
        if (file.write(bytes) != bytes.size()) {
            failure = file.errorString();
            active->abort();
            return;
        }
        hash.addData(bytes);
    }
    emit progress(received);
}
} // namespace airlet
