#pragma once
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace airlet {
int compareDocumentNames(const QString &a, const QString &b);
class FolderNavigator final : public QObject {
    Q_OBJECT
  public:
    explicit FolderNavigator(QObject *parent = nullptr);
    ~FolderNavigator() override;
    quint64 request(const QString &current);
  signals:
    void resolved(quint64 request, QString current, QString next);

  private:
    std::mutex mutex;
    std::condition_variable condition;
    QString pending;
    std::atomic<quint64> generation{0};
    bool stopping = false;
    std::jthread thread;
};
} // namespace airlet
