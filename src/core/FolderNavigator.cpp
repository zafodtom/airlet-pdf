#include "FolderNavigator.h"
#include <QDirIterator>
#include <QFileInfo>
#include <utility>
namespace airlet {
int compareDocumentNames(const QString &a, const QString &b) {
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            auto ae = i, be = j;
            while (ae < a.size() && a[ae].isDigit())
                ++ae;
            while (be < b.size() && b[be].isDigit())
                ++be;
            auto ai = i, bj = j;
            while (ai + 1 < ae && a[ai] == QChar('0'))
                ++ai;
            while (bj + 1 < be && b[bj] == QChar('0'))
                ++bj;
            if (ae - ai != be - bj)
                return ae - ai < be - bj ? -1 : 1;
            const auto cmp = QString::compare(a.mid(ai, ae - ai), b.mid(bj, be - bj));
            if (cmp)
                return cmp;
            i = ae;
            j = be;
        } else {
            const auto ac = a[i].toCaseFolded(), bc = b[j].toCaseFolded();
            if (ac != bc)
                return ac < bc ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size())
        return 1;
    if (j < b.size())
        return -1;
    return QString::compare(a, b, Qt::CaseSensitive);
}
FolderNavigator::FolderNavigator(QObject *parent) : QObject(parent) {}
FolderNavigator::~FolderNavigator() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
        ++generation;
    }
    condition.notify_one();
    if (thread.joinable())
        thread.join();
}
quint64 FolderNavigator::request(const QString &current) {
    std::lock_guard lock(mutex);
    const auto id = ++generation;
    pending = current;
    if (!thread.joinable())
        thread = std::jthread([this] {
            for (;;) {
                QString current;
                quint64 id;
                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [this] { return stopping || !pending.isEmpty(); });
                    if (stopping)
                        return;
                    current = std::exchange(pending, {});
                    id = generation.load();
                }
                const QFileInfo source(current);
                QString next;
                QDirIterator it(source.absolutePath(), QDir::Files | QDir::Readable);
                while (it.hasNext() && generation.load() == id) {
                    it.next();
                    const auto file = it.fileInfo();
                    if (file.suffix().compare("pdf", Qt::CaseInsensitive) != 0 ||
                        compareDocumentNames(file.fileName(), source.fileName()) <= 0)
                        continue;
                    if (next.isEmpty() ||
                        compareDocumentNames(file.fileName(), QFileInfo(next).fileName()) < 0)
                        next = file.absoluteFilePath();
                }
                if (generation.load() == id)
                    emit resolved(id, current, next);
            }
        });
    condition.notify_one();
    return id;
}
} // namespace airlet
