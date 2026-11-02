#pragma once
#include <QString>
#include <QVector>
namespace airlet {
QVector<int> parsePageRange(QString range, int count, QString *error);
int runExportWorker(int argc, char **argv);
} // namespace airlet
