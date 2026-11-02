#pragma once
#include <QJsonObject>
#include <QUrl>
namespace airlet {
struct UpdateRelease {
    QString version, installerName, sha256;
    QUrl manifestUrl, installerUrl;
    qint64 size = 0;
};
enum class ReleaseState { Invalid, Current, Newer };
bool allowedUpdateUrl(const QUrl &url);
ReleaseState parseUpdateRelease(const QJsonObject &json, const QString &current,
                                UpdateRelease *release, QString *error);
bool validateUpdateManifest(const QJsonObject &json, UpdateRelease *release, QString *error);
} // namespace airlet
