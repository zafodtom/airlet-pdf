#include "Language.h"
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>
static void initTranslations() {
    Q_INIT_RESOURCE(airlet_translations);
}
namespace airlet {
static bool english = false;
static QHash<QString, QString> translations;
void setLanguage(const QString &code) {
    english = code == "en";
    if (english && translations.isEmpty()) {
        initTranslations();
        QFile file(":/i18n/en.json");
        if (file.open(QIODevice::ReadOnly)) {
            const auto table = QJsonDocument::fromJson(file.readAll()).object();
            for (auto it = table.begin(); it != table.end(); ++it)
                translations.insert(it.key(), it.value().toString());
        }
    }
}
QString languageCode() {
    return english ? QStringLiteral("en") : QStringLiteral("cs");
}
QString uiText(const QString &source) {
    return english ? translations.value(source, source) : source;
}
} // namespace airlet
