#include "RenderService.h"
#include "core/Geometry.h"
#include <QElapsedTimer>
#include <QPainter>
namespace airlet {
RenderService::RenderService(QObject *parent, Factory factory) : QObject(parent) {
    thread = std::jthread([this, factory = std::move(factory)] { run(factory); });
}
RenderService::~RenderService() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
        ++generation;
        ++documentEpoch;
        batch.requests.clear();
    }
    condition.notify_one();
    thread.join(); // engine and QFile are destroyed on their owning worker
}
void RenderService::open(quint64 session, const QString &path, const QString &password) {
    {
        std::lock_guard lock(mutex);
        ++generation;
        ++documentEpoch;
        pendingOpen = OpenCommand{session, path, password};
        batch.requests.clear();
        pendingText.reset();
        pendingArea.reset();
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
void RenderService::requestText(quint64 session, int page, quint64 request) {
    {
        std::lock_guard lock(mutex);
        pendingText = TextCommand{session, request, page};
    }
    condition.notify_one();
}
void RenderService::requestArea(quint64 session, quint64 request, RenderRequest area) {
    {
        std::lock_guard lock(mutex);
        areaGeneration = request;
        pendingArea = AreaCommand{session, request, documentEpoch.load(), std::move(area)};
    }
    condition.notify_one();
}
void RenderService::cancelArea() {
    {
        std::lock_guard lock(mutex);
        ++areaGeneration;
        pendingArea.reset();
    }
    condition.notify_one();
}
void RenderService::run(Factory factory) {
    auto engine = factory();
    quint64 activeSession = 0;
    struct AreaWork {
        AreaCommand command;
        QImage image;
        int x = 0, y = 0, done = 0, total = 0;
    };
    std::optional<AreaWork> areaWork;
    for (;;) {
        if (areaWork && (areaWork->command.epoch != documentEpoch.load() ||
                         areaWork->command.request != areaGeneration.load()))
            areaWork.reset();
        std::optional<OpenCommand> opening;
        std::optional<TextCommand> textCommand;
        std::optional<AreaCommand> areaCommand;
        bool areaTile = false;
        RenderRequest request;
        quint64 session = 0, gen = 0;
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this, &areaWork] {
                return stopping || pendingOpen || pendingText || pendingArea ||
                       !batch.requests.empty() || areaWork;
            });
            if (stopping)
                break;
            if (pendingOpen) {
                opening = std::move(pendingOpen);
                pendingOpen.reset();
            } else if (pendingText) {
                textCommand = std::move(pendingText);
                pendingText.reset();
            } else if (pendingArea) {
                areaCommand = std::move(pendingArea);
                pendingArea.reset();
            } else if (!batch.requests.empty()) {
                session = batch.session;
                gen = batch.generation;
                request = std::move(batch.requests.front());
                batch.requests.pop_front();
            } else
                areaTile = true;
        }
        if (opening) {
            areaWork.reset();
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
        if (textCommand) {
            if (textCommand->session != activeSession)
                continue;
            const auto epoch = documentEpoch.load();
            ++started;
            auto text = engine->text(textCommand->page);
            ++finished;
            if (epoch != documentEpoch.load())
                continue;
            QMetaObject::invokeMethod(
                this,
                [this, c = *textCommand, text = std::move(text)] {
                    emit textReady(c.session, c.request, c.page, text);
                },
                Qt::QueuedConnection);
            continue;
        }
        if (areaCommand) {
            const auto &c = *areaCommand;
            if (c.session != activeSession || c.epoch != documentEpoch.load() ||
                c.request != areaGeneration.load())
                continue;
            const auto size = c.area.tile.size();
            if (size.isEmpty() || qint64(size.width()) * size.height() > 16 * 1024 * 1024 ||
                !QRect(QPoint(), c.area.fullPixels).contains(c.area.tile)) {
                QMetaObject::invokeMethod(
                    this,
                    [this, c] {
                        emit areaReady(c.session, c.request, {},
                                       QStringLiteral("Neplatný nebo příliš velký výřez."));
                    },
                    Qt::QueuedConnection);
                continue;
            }
            QImage image(size, QImage::Format_ARGB32);
            if (image.isNull()) {
                QMetaObject::invokeMethod(
                    this,
                    [this, c] {
                        emit areaReady(c.session, c.request, {},
                                       QStringLiteral("Pro výřez není dostatek paměti."));
                    },
                    Qt::QueuedConnection);
                continue;
            }
            image.fill(Qt::white);
            const auto points = rotatedSize(engine->pageInfo(c.area.page).points, c.area.rotation);
            if (points.isValid()) {
                image.setDotsPerMeterX(
                    qRound(c.area.fullPixels.width() / points.width() * 72. / .0254));
                image.setDotsPerMeterY(
                    qRound(c.area.fullPixels.height() / points.height() * 72. / .0254));
            }
            areaWork = AreaWork{c, std::move(image),
                                0, 0,
                                0, ((size.width() + 511) / 512) * ((size.height() + 511) / 512)};
            continue;
        }
        if (areaTile) {
            if (!areaWork)
                continue;
            auto &work = *areaWork;
            const auto c = work.command;
            const auto cancelled = [this, c] {
                return c.epoch != documentEpoch.load() || c.request != areaGeneration.load();
            };
            if (cancelled()) {
                areaWork.reset();
                continue;
            }
            auto tile = c.area;
            tile.tile = QRect(c.area.tile.x() + work.x, c.area.tile.y() + work.y,
                              std::min(512, work.image.width() - work.x),
                              std::min(512, work.image.height() - work.y));
            ++started;
            auto image = engine->render(tile, cancelled);
            ++finished;
            if (cancelled()) {
                areaWork.reset();
                continue;
            }
            if (image.isNull()) {
                areaWork.reset();
                QMetaObject::invokeMethod(
                    this,
                    [this, c] {
                        emit areaReady(c.session, c.request, {},
                                       QStringLiteral("Výřez nelze vykreslit."));
                    },
                    Qt::QueuedConnection);
                continue;
            }
            {
                QPainter painter(&work.image);
                painter.drawImage(QPoint(work.x, work.y), image);
            }
            ++work.done;
            work.x += 512;
            if (work.x >= work.image.width()) {
                work.x = 0;
                work.y += 512;
            }
            const int percent = 100 * work.done / work.total;
            QMetaObject::invokeMethod(
                this, [this, c, percent] { emit areaProgress(c.session, c.request, percent); },
                Qt::QueuedConnection);
            if (work.y >= work.image.height()) {
                auto output = std::move(work.image);
                areaWork.reset();
                QMetaObject::invokeMethod(
                    this, [this, c, output] { emit areaReady(c.session, c.request, output, {}); },
                    Qt::QueuedConnection);
            }
            continue; // visible render batches can run between crop tiles
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
