#include "RenderService.h"
#include <QElapsedTimer>
namespace airlet {
RenderService::RenderService(QObject *parent, Factory factory) : QObject(parent) {
    thread = std::jthread([this, factory = std::move(factory)] { run(factory); });
}
RenderService::~RenderService() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
        ++generation;
        batch.requests.clear();
    }
    condition.notify_one();
    thread.join(); // engine and QFile are destroyed on their owning worker
}
void RenderService::open(quint64 session, const QString &path, const QString &password) {
    {
        std::lock_guard lock(mutex);
        ++generation;
        pendingOpen = OpenCommand{session, path, password};
        batch.requests.clear();
    }
    condition.notify_one();
}
void RenderService::close(quint64 session) {
    open(session, {});
}
quint64 RenderService::submit(quint64 session, QVector<RenderRequest> requests) {
    quint64 gen;
    {
        std::lock_guard lock(mutex);
        gen = ++generation;
        batch = {session, gen, {requests.begin(), requests.end()}};
    }
    condition.notify_one();
    return gen;
}
void RenderService::run(Factory factory) {
    auto engine = factory();
    quint64 activeSession = 0;
    for (;;) {
        std::optional<OpenCommand> opening;
        RenderRequest request;
        quint64 session = 0, gen = 0;
        {
            std::unique_lock lock(mutex);
            condition.wait(lock,
                           [this] { return stopping || pendingOpen || !batch.requests.empty(); });
            if (stopping)
                break;
            if (pendingOpen) {
                opening = std::move(pendingOpen);
                pendingOpen.reset();
            } else {
                session = batch.session;
                gen = batch.generation;
                request = std::move(batch.requests.front());
                batch.requests.pop_front();
            }
        }
        if (opening) {
            engine->close();
            activeSession = opening->session;
            if (opening->path.isEmpty())
                continue;
            auto result = engine->open(opening->path, opening->password);
            QMetaObject::invokeMethod(
                this, [this, s = activeSession, result] { emit opened(s, result); },
                Qt::QueuedConnection);
            continue;
        }
        if (session != activeSession || gen != generation.load())
            continue;
        ++started;
        QElapsedTimer timer;
        timer.start();
        auto info = engine->pageInfo(request.page);
        auto image = engine->render(request, [this, gen] { return generation.load() != gen; });
        ++finished;
        if (gen != generation.load())
            continue;
        QMetaObject::invokeMethod(
            this,
            [this, session, gen, request, info, image, ms = timer.elapsed()] {
                emit rendered(session, gen, request, info, image, ms);
            },
            Qt::QueuedConnection);
    }
}
} // namespace airlet
