#include "assets/ThumbnailCache.h"

#include "logging/UiPerformanceMonitor.h"

#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QtConcurrent>

#include <algorithm>
#include <limits>

namespace tlm {

namespace {
constexpr qsizetype kCacheBudgetBytes = 96 * 1024 * 1024;
constexpr int kMaximumVariantsPerImage = 2;
constexpr int kDefaultRequestSide = 192;
constexpr int kMaxCachedDecodeSide = 2048;
constexpr qreal kNativeLongSideRatio = 0.72;
constexpr qint64 kNativeAreaRatioNumerator = 55;
constexpr qint64 kNativeAreaRatioDenominator = 100;

int bucketExtent(int value) {
    value = qBound(1, value, kMaxCachedDecodeSide);
    const int step = value <= 512 ? 64 : 128;
    return qMin(kMaxCachedDecodeSide, ((value + step - 1) / step) * step);
}

QSize adaptiveDecodeSize(const QSize& sourceSize, QSize requestedSize) {
    if (!sourceSize.isValid() || requestedSize.isEmpty()) {
        return requestedSize;
    }

    requestedSize = requestedSize.expandedTo(QSize(1, 1));
    const int sourceLong = qMax(sourceSize.width(), sourceSize.height());
    const int requestedLong = qMax(requestedSize.width(), requestedSize.height());
    const qint64 sourceArea = static_cast<qint64>(sourceSize.width()) * sourceSize.height();
    const qint64 requestedArea =
        static_cast<qint64>(requestedSize.width()) * requestedSize.height();

    // Decode native pixels only when the display is close enough to native size, or when
    // the original is modest. Otherwise QImageReader scales during decode to avoid loading
    // a huge bitmap just to downsample it immediately in QPainter.
    const bool nearNative =
        requestedLong >= qRound(sourceLong * kNativeLongSideRatio) ||
        (sourceArea <= 4'000'000 &&
         requestedArea >= (sourceArea * kNativeAreaRatioNumerator) / kNativeAreaRatioDenominator);
    if (nearNative) {
        return sourceSize;
    }

    const QSize oversampled(qRound(requestedSize.width() * 1.18),
                            qRound(requestedSize.height() * 1.18));
    return sourceSize.scaled(
        oversampled.boundedTo(QSize(kMaxCachedDecodeSide, kMaxCachedDecodeSide)),
        Qt::KeepAspectRatio);
}
} // namespace

ThumbnailCache::ThumbnailCache(QObject* parent, qsizetype cacheBudgetBytes)
    : QObject(parent),
      m_cacheBudgetBytes(cacheBudgetBytes > 0 ? cacheBudgetBytes : kCacheBudgetBytes) {}

QPixmap ThumbnailCache::thumbnail(const QString& cacheKey) const {
    return bestThumbnail(cacheKey, {});
}

QPixmap ThumbnailCache::thumbnail(const QString& cacheKey, QSize minimumPixelSize) const {
    return bestThumbnail(cacheKey, normalizedRequestSize(minimumPixelSize));
}

bool ThumbnailCache::hasThumbnail(const QString& cacheKey) const {
    return !bestThumbnail(cacheKey, {}).isNull();
}

bool ThumbnailCache::hasThumbnail(const QString& cacheKey, QSize minimumPixelSize) const {
    minimumPixelSize = normalizedRequestSize(minimumPixelSize);
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailCoverageCheck);
    auto best = m_cache.end();
    qint64 bestArea = std::numeric_limits<qint64>::max();
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        const CacheEntry& entry = it.value();
        if (entry.baseKey != cacheKey || entry.pixmap.isNull()) {
            continue;
        }
        // requestSize describes the decode quality. The decoded pixmap keeps its aspect ratio,
        // so comparing both pixmap dimensions against a square paint request permanently marks
        // wide and tall images as undersized.
        const QSize coveredSize = entry.requestSize;
        if (coveredSize.width() >= minimumPixelSize.width() &&
            coveredSize.height() >= minimumPixelSize.height()) {
            const qint64 area = static_cast<qint64>(coveredSize.width()) * coveredSize.height();
            if (area < bestArea) {
                bestArea = area;
                best = it;
            }
        }
    }
    if (best != m_cache.end()) {
        best.value().serial = ++m_serial;
        UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailCoverageHit);
        return true;
    }
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailCoverageMiss);
    return false;
}

void ThumbnailCache::requestThumbnail(const QString& cacheKey, const QString& filePath,
                                      QSize size) {
    const QSize requestSize = normalizedRequestSize(size);
    const QString key = variantKey(cacheKey, requestSize);
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailRequest);
    if (m_cache.contains(key)) {
        UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailRequestCached);
        return;
    }
    if (m_pending.contains(key)) {
        UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailRequestPending);
        return;
    }
    if (filePath.isEmpty()) {
        return;
    }
    m_pending.insert(key);
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailRequestStarted);
    const quint64 generation = m_generation;

    auto* watcher = new QFutureWatcher<QImage>(static_cast<QObject*>(this));
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, cacheKey, requestSize, key, generation]() {
                const QImage image = watcher->result();
                watcher->deleteLater();
                // A project switch invalidates in-flight work. Do not let an older decode erase
                // or overwrite a request that belongs to the new project generation.
                if (generation != m_generation) {
                    return;
                }
                m_pending.remove(key);
                if (image.isNull()) {
                    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailFailed);
                    emit thumbnailFailed(cacheKey, tr("Thumbnail generation failed."));
                    return;
                }
                insertEntry(cacheKey, requestSize, QPixmap::fromImage(image));
                UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailReady);
                emit thumbnailReady(cacheKey);
            });

    watcher->setFuture(QtConcurrent::run([filePath, requestSize]() {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        const QSize sourceSize = reader.size();
        if (sourceSize.isValid() && !requestSize.isEmpty()) {
            const QSize decodeSize = adaptiveDecodeSize(sourceSize, requestSize);
            if (decodeSize.isValid() && decodeSize != sourceSize) {
                reader.setScaledSize(sourceSize.scaled(decodeSize, Qt::KeepAspectRatio));
            }
        }
        return reader.read();
    }));
}

void ThumbnailCache::clear() {
    ++m_generation;
    m_cache.clear();
    m_pending.clear();
    m_cacheCostBytes = 0;
}

QSize ThumbnailCache::normalizedRequestSize(QSize size) {
    if (size.isEmpty()) {
        size = QSize(kDefaultRequestSide, kDefaultRequestSide);
    }
    return QSize(bucketExtent(size.width()), bucketExtent(size.height()));
}

QString ThumbnailCache::variantKey(const QString& cacheKey, QSize requestSize) {
    return QStringLiteral("%1@%2x%3")
        .arg(cacheKey)
        .arg(requestSize.width())
        .arg(requestSize.height());
}

qsizetype ThumbnailCache::pixmapCostBytes(const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return 0;
    }
    return static_cast<qsizetype>(pixmap.width()) * pixmap.height() * 4;
}

QPixmap ThumbnailCache::bestThumbnail(const QString& cacheKey, QSize minimumPixelSize) const {
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailPixmapLookup);
    const bool hasMinimum = !minimumPixelSize.isEmpty();
    auto bestCovering = m_cache.end();
    auto largestFallback = m_cache.end();
    qint64 bestCoveringArea = std::numeric_limits<qint64>::max();
    qint64 largestArea = -1;

    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        const CacheEntry& entry = it.value();
        if (entry.baseKey != cacheKey || entry.pixmap.isNull()) {
            continue;
        }
        const QSize pixmapSize = entry.pixmap.size();
        const qint64 area = static_cast<qint64>(pixmapSize.width()) * pixmapSize.height();
        if (area > largestArea) {
            largestArea = area;
            largestFallback = it;
        }
        const QSize coveredSize = entry.requestSize;
        const qint64 coveringArea = static_cast<qint64>(coveredSize.width()) * coveredSize.height();
        if (hasMinimum && coveredSize.width() >= minimumPixelSize.width() &&
            coveredSize.height() >= minimumPixelSize.height() && coveringArea < bestCoveringArea) {
            bestCoveringArea = coveringArea;
            bestCovering = it;
        }
        if (!hasMinimum && area < bestCoveringArea) {
            bestCoveringArea = area;
            bestCovering = it;
        }
    }

    auto selected = bestCovering != m_cache.end() ? bestCovering : largestFallback;
    if (selected != m_cache.end()) {
        selected.value().serial = ++m_serial;
        return selected.value().pixmap;
    }
    return {};
}

void ThumbnailCache::insertEntry(const QString& cacheKey, QSize requestSize,
                                 const QPixmap& pixmap) {
    const QString key = variantKey(cacheKey, requestSize);
    const qsizetype nextCost = pixmapCostBytes(pixmap);
    if (m_cache.contains(key)) {
        m_cacheCostBytes -= m_cache.value(key).costBytes;
    }
    m_cache.insert(key, CacheEntry{cacheKey, requestSize, pixmap, nextCost, ++m_serial});
    m_cacheCostBytes += nextCost;
    pruneRedundantVariants(cacheKey);
    trimToBudget();
}

void ThumbnailCache::pruneRedundantVariants(const QString& cacheKey) {
    while (true) {
        QString baselineKey;
        qint64 baselineArea = std::numeric_limits<qint64>::max();
        int variantCount = 0;
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const CacheEntry& entry = it.value();
            if (entry.baseKey != cacheKey) {
                continue;
            }
            ++variantCount;
            const qint64 area =
                static_cast<qint64>(entry.requestSize.width()) * entry.requestSize.height();
            if (area < baselineArea) {
                baselineArea = area;
                baselineKey = it.key();
            }
        }
        if (variantCount <= kMaximumVariantsPerImage) {
            return;
        }

        QString victimKey;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            if (it.value().baseKey == cacheKey && it.key() != baselineKey &&
                it.value().serial < oldest) {
                oldest = it.value().serial;
                victimKey = it.key();
            }
        }
        if (victimKey.isEmpty()) {
            return;
        }
        removeEntry(victimKey);
    }
}

void ThumbnailCache::removeEntry(const QString& key) {
    const auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return;
    }
    m_cacheCostBytes -= it.value().costBytes;
    UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailEvicted);
    m_cache.erase(it);
}

void ThumbnailCache::trimToBudget() {
    while (m_cacheCostBytes > m_cacheBudgetBytes && m_cache.size() > 1) {
        QHash<QString, int> variantCounts;
        QHash<QString, QString> baselineKeys;
        QHash<QString, qint64> baselineAreas;
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const CacheEntry& entry = it.value();
            ++variantCounts[entry.baseKey];
            const qint64 area =
                static_cast<qint64>(entry.requestSize.width()) * entry.requestSize.height();
            if (!baselineAreas.contains(entry.baseKey) ||
                area < baselineAreas.value(entry.baseKey)) {
                baselineAreas.insert(entry.baseKey, area);
                baselineKeys.insert(entry.baseKey, it.key());
            }
        }

        QString victimKey;
        quint64 oldest = std::numeric_limits<quint64>::max();
        // Detail variants are expendable; retaining one baseline prevents visible images from
        // falling back to a loading glyph while a larger hover rendition is decoded.
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            const bool redundant = variantCounts.value(it.value().baseKey) > 1 &&
                                   baselineKeys.value(it.value().baseKey) != it.key();
            if (redundant && it.value().serial < oldest) {
                oldest = it.value().serial;
                victimKey = it.key();
            }
        }
        if (victimKey.isEmpty()) {
            for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
                if (it.value().serial < oldest) {
                    oldest = it.value().serial;
                    victimKey = it.key();
                }
            }
        }
        if (victimKey.isEmpty()) {
            return;
        }
        removeEntry(victimKey);
    }
}

} // namespace tlm
