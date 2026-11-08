#include "UpdateDialog.h"
#include "i18n/Language.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
using namespace airlet;
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
