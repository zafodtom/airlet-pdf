#pragma once
#include "core/PdfEngine.h"
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace airlet {
class RenderService final : public QObject {
    Q_OBJECT
  public:
    using Factory = std::function<std::unique_ptr<PdfEngine>()>;
    explicit RenderService(QObject *parent = nullptr, Factory factory = makePdfiumEngine);
    ~RenderService() override;
    void open(quint64 session, const QString &path, const QString &password = {});
    void close(quint64 session);
    quint64 submit(quint64 session, QVector<RenderRequest> requests);
    quint64 jobsStarted() const {
        return started.load();
    }
    quint64 jobsFinished() const {
        return finished.load();
    }
  signals:
    void opened(quint64 session, airlet::OpenResult result);
    void rendered(quint64 session, quint64 generation, airlet::RenderRequest request,
                  airlet::PageInfo info, QImage image, qint64 elapsedMs);

  private:
    struct OpenCommand {
        quint64 session;
        QString path, password;
    };
    struct Batch {
        quint64 session, generation;
        std::deque<RenderRequest> requests;
    };
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<OpenCommand> pendingOpen;
    Batch batch{};
    bool stopping = false;
    std::atomic<quint64> generation{0}, started{0}, finished{0};
    std::jthread thread;
    void run(Factory factory);
};
} // namespace airlet
Q_DECLARE_METATYPE(airlet::OpenResult)
Q_DECLARE_METATYPE(airlet::RenderRequest)
Q_DECLARE_METATYPE(airlet::PageInfo)
