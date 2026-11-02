#pragma once
#include <QCache>
#include <QImage>
#include <QString>
namespace airlet {
// KiB accounting avoids QCache's int limit. Both stores have hard limits.
class TileCache {
    QCache<QString, QImage> tiles;

  public:
    explicit TileCache(int limitMiB = 96) : tiles(limitMiB * 1024) {}
    bool contains(const QString &key) const {
        return tiles.contains(key);
    }
    const QImage *get(const QString &key) {
        return tiles.object(key);
    }
    void put(const QString &key, const QImage &image) {
        if (image.isNull())
            return;
        const auto cost = (image.sizeInBytes() + 1023) / 1024;
        if (cost > tiles.maxCost())
            return;
        tiles.insert(key, new QImage(image), static_cast<int>(cost));
    }
    void clear() {
        tiles.clear();
    }
    qint64 bytes() const {
        return qint64(tiles.totalCost()) * 1024;
    }
    qint64 limitBytes() const {
        return qint64(tiles.maxCost()) * 1024;
    }
};
} // namespace airlet
