#pragma once
#include "UpdateDownload.h"
#include "UpdateProtocol.h"
#include <QDialog>
#include <QJsonObject>
#include <functional>
class QLabel;
class QProgressBar;
class QPushButton;
namespace airlet {
class UpdateDialog final : public QDialog {
  public:
    explicit UpdateDialog(const QJsonObject &job, QNetworkAccessManager *manager = nullptr);
    ~UpdateDialog() override;

  private:
    UpdateDownload transfer;
    std::function<void()> completion;
    QLabel *status;
    QProgressBar *progress;
    QPushButton *check, *download;
    QString version, folder, phase;
    UpdateRelease release;
    QByteArray buffer;
    qint64 received = 0;
    QString downloadedHash;
    bool binary = false, ready = false;
    void fail(QString error);
    void fetch(QUrl url, qint64 maximum, bool toFile, std::function<void()> completed,
               QUrl fallback = {});
    void showAvailableUpdate();
    void startCheck();
};
} // namespace airlet
