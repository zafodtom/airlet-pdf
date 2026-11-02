#pragma once
#include <QImage>
#include <QObject>
#include <atomic>
#include <thread>

namespace airlet {
class ImageSaveJob final : public QObject {
    Q_OBJECT
  public:
    explicit ImageSaveJob(QObject *parent = nullptr) : QObject(parent) {}
    ~ImageSaveJob() override;
    bool start(QImage image, QString target, QByteArray format, QString sourcePdf, bool overwrite,
               quint64 request = 0);
    void cancel();
    bool isRunning() const {
        return running.load();
    }
  signals:
    void finished(quint64 request, bool success, bool cancelled, QString error);

  private:
    std::jthread worker;
    std::atomic<bool> running{false};
};
} // namespace airlet
