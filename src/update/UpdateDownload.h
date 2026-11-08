#pragma once
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
namespace airlet {
// Used only by the manually launched updater, never by the PDF viewer.
class UpdateDownload final : public QObject {
    Q_OBJECT
  public:
    explicit UpdateDownload(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);
    ~UpdateDownload() override;
    void start(QUrl primary, QUrl fallback, qint64 maximum, QString destination = {});
    void cancel();
  signals:
    void completed(QByteArray data, qint64 bytes, QString sha256);
    void failed(QString error);
    void progress(qint64 bytes);
    void retrying();

  private:
    QNetworkAccessManager *network;
    QPointer<QNetworkReply> reply;
    QTimer retry;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QUrl primary, fallback;
    QString destination, failure;
    QByteArray buffer;
    qint64 received = 0, maximum = 0;
    int attempt = 0;
    bool ownsFile = false;
    void begin();
    void drain(QNetworkReply *active);
    void discard();
};
} // namespace airlet
