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

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {}

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
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const CacheEntry& entry = it.value();
        if (entry.baseKey != cacheKey || entry.pixmap.isNull()) {
            continue;
        }
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        // requestSize describes the decode quality. The decoded pixmap keeps its aspect ratio,
        // so comparing both pixmap dimensions against a square paint request permanently marks
        // wide and tall images as undersized.
        const QSize coveredSize = entry.requestSize;
#else
        const QSize pixmapSize = entry.pixmap.size();
        const QSize coveredSize = pixmapSize;
#endif
        if (coveredSize.width() >= minimumPixelSize.width() &&
            coveredSize.height() >= minimumPixelSize.height()) {
            UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailCoverageHit);
            return true;
        }
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
    const CacheEntry* bestCovering = nullptr;
    const CacheEntry* largestFallback = nullptr;
    qint64 bestCoveringArea = std::numeric_limits<qint64>::max();
    qint64 largestArea = -1;

    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const CacheEntry& entry = it.value();
        if (entry.baseKey != cacheKey || entry.pixmap.isNull()) {
            continue;
        }
        const QSize pixmapSize = entry.pixmap.size();
        const qint64 area = static_cast<qint64>(pixmapSize.width()) * pixmapSize.height();
        if (area > largestArea) {
            largestArea = area;
            largestFallback = &entry;
        }
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        const QSize coveredSize = entry.requestSize;
        const qint64 coveringArea = static_cast<qint64>(coveredSize.width()) * coveredSize.height();
#else
        const QSize coveredSize = pixmapSize;
        const qint64 coveringArea = area;
#endif
        if (hasMinimum && coveredSize.width() >= minimumPixelSize.width() &&
            coveredSize.height() >= minimumPixelSize.height() && coveringArea < bestCoveringArea) {
            bestCoveringArea = coveringArea;
            bestCovering = &entry;
        }
        if (!hasMinimum && area < bestCoveringArea) {
            bestCoveringArea = area;
            bestCovering = &entry;
        }
    }

    if (bestCovering) {
        return bestCovering->pixmap;
    }
    if (largestFallback) {
        return largestFallback->pixmap;
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
    trimToBudget();
}

void ThumbnailCache::trimToBudget() {
    while (m_cacheCostBytes > kCacheBudgetBytes && m_cache.size() > 1) {
        auto victim = m_cache.end();
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it.value().serial < oldest) {
                oldest = it.value().serial;
                victim = it;
            }
        }
        if (victim == m_cache.end()) {
            break;
        }
        m_cacheCostBytes -= victim.value().costBytes;
        UiPerformanceMonitor::increment(UiPerformanceMonitor::Counter::ThumbnailEvicted);
        m_cache.erase(victim);
    }
}

} // namespace tlm
