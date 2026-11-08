#include "UpdateProtocol.h"
#include "i18n/Language.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QVersionNumber>
#include <cmath>
namespace airlet {
bool allowedUpdateUrl(const QUrl &u) {
    return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
           (u.port(-1) == -1 || u.port() == 443) &&
           QStringList{"api.github.com", "github.com", "release-assets.githubusercontent.com",
                       "objects.githubusercontent.com"}
               .contains(u.host());
}
static bool versionValid(const QString &s) {
    return QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(s).hasMatch() &&
           QVersionNumber::fromString(s).toString() == s;
}
ReleaseState parseUpdateRelease(const QJsonObject &json, const QString &current, UpdateRelease *r,
                                QString *error) {
    *r = {};
    error->clear();
    QString tag = json["tag_name"].toString();
    r->version = tag.startsWith('v') ? tag.mid(1) : tag;
    if (json["draft"].toBool() || json["prerelease"].toBool() || !versionValid(r->version) ||
        !versionValid(current)) {
        *error = uiText(QStringLiteral("Neplatná stabilní verze aktualizace."));
        return ReleaseState::Invalid;
    }
    if (QVersionNumber::compare(QVersionNumber::fromString(r->version),
                                QVersionNumber::fromString(current)) <= 0)
        return ReleaseState::Current;
    r->installerName = QStringLiteral("AirletPDF-Setup-%1-x64.exe").arg(r->version);
    int manifests = 0, installers = 0;
    const auto prefix = QStringLiteral("/zafodtom/airlet-pdf/releases/download/%1/").arg(tag);
    for (const auto &v : json["assets"].toArray()) {
        const auto a = v.toObject();
        const auto name = a["name"].toString();
        if (name != "airlet-update.json" && name != r->installerName)
            continue;
        const QUrl url(a["browser_download_url"].toString());
        if (!allowedUpdateUrl(url) || url.host() != "github.com" || url.path() != prefix + name ||
            !url.query().isEmpty() || !url.fragment().isEmpty()) {
            *error = uiText(QStringLiteral("Neplatná adresa aktualizace."));
            return ReleaseState::Invalid;
        }
        const QUrl api(a["url"].toString());
        if (!api.isEmpty() &&
            (!allowedUpdateUrl(api) || api.host() != "api.github.com" ||
             !QRegularExpression("^/repos/zafodtom/airlet-pdf/releases/assets/[1-9][0-9]*$")
                  .match(api.path())
                  .hasMatch() ||
             !api.query().isEmpty() || !api.fragment().isEmpty())) {
            *error = uiText(QStringLiteral("Neplatná adresa aktualizace."));
            return ReleaseState::Invalid;
        }
        if (name == "airlet-update.json") {
            ++manifests;
            r->manifestUrl = url;
            r->manifestApiUrl = api;
        } else {
            ++installers;
            r->installerUrl = url;
            r->installerApiUrl = api;
            const double bytes = a["size"].toDouble(-1);
            r->size = std::isfinite(bytes) && bytes >= 1 && bytes <= 512. * 1024 * 1024 &&
                              std::floor(bytes) == bytes
                          ? qint64(bytes)
                          : -1;
            const auto digest = a["digest"].toString();
            if (digest.startsWith("sha256:"))
                r->sha256 = digest.mid(7).toLower();
        }
    }
    if (manifests != 1 || installers != 1 || r->size <= 0 || r->size > 512LL * 1024 * 1024) {
        *error = uiText(QStringLiteral("Vydání nemá úplný aktualizační balíček."));
        return ReleaseState::Invalid;
    }
    return ReleaseState::Newer;
}
bool validateUpdateManifest(const QJsonObject &json, UpdateRelease *r, QString *error) {
    const auto installer = json["installer"].toObject();
    const auto hash = installer["sha256"].toString().toLower();
    if (json["schema"].toInt() != 1 || json["version"].toString() != r->version ||
        installer["name"].toString() != r->installerName ||
        installer["size"].toDouble(-1) != double(r->size) ||
        !QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch() ||
        (!r->sha256.isEmpty() && r->sha256 != hash)) {
        *error = uiText(QStringLiteral("Nesouhlasí údaje nebo kontrolní součet aktualizace."));
        return false;
    }
    r->sha256 = hash;
    return true;
}
} // namespace airlet
