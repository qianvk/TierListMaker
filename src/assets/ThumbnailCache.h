#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

namespace tlm {

/** Multi-resolution image cache that decodes only as much quality as the current paint size needs. */
class ThumbnailCache : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailCache(QObject* parent = nullptr);

    QPixmap thumbnail(const QString& cacheKey) const;
    QPixmap thumbnail(const QString& cacheKey, QSize minimumPixelSize) const;
    bool hasThumbnail(const QString& cacheKey) const;
    bool hasThumbnail(const QString& cacheKey, QSize minimumPixelSize) const;
    void requestThumbnail(const QString& cacheKey, const QString& filePath, QSize size);
    void clear();

signals:
    void thumbnailReady(const QString& cacheKey);
    void thumbnailFailed(const QString& cacheKey, const QString& reason);

private:
    struct CacheEntry {
        QString baseKey;
        QSize requestSize;
        QPixmap pixmap;
        qsizetype costBytes{0};
        quint64 serial{0};
    };

    static QSize normalizedRequestSize(QSize size);
    static QString variantKey(const QString& cacheKey, QSize requestSize);
    static qsizetype pixmapCostBytes(const QPixmap& pixmap);
    QPixmap bestThumbnail(const QString& cacheKey, QSize minimumPixelSize) const;
    void insertEntry(const QString& cacheKey, QSize requestSize, const QPixmap& pixmap);
    void trimToBudget();

    QHash<QString, CacheEntry> m_cache;
    QSet<QString> m_pending;
    qsizetype m_cacheCostBytes{0};
    quint64 m_serial{0};
    quint64 m_generation{0};
};

} // namespace tlm
