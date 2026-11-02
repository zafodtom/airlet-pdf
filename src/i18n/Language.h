#pragma once
#include <QString>
namespace airlet {
// Set once at process startup, before starting any workers.
void setLanguage(const QString &code);
QString languageCode();
QString uiText(const QString &source);
} // namespace airlet
