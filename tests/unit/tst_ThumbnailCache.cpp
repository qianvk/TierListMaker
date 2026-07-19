#include "assets/CoverImageCache.h"
#include "assets/ThumbnailCache.h"

#include <QColor>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tlm;

class ThumbnailCacheTest final : public QObject {
    Q_OBJECT

private slots:
    void detailEvictionRetainsFallbacksAndUsesRecentAccess();
    void coverRenderingIsReusedUntilPaintSizeChanges();
};

void ThumbnailCacheTest::detailEvictionRetainsFallbacksAndUsesRecentAccess() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QStringList ids{QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")};
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    QStringList paths;
    for (int index = 0; index < ids.size(); ++index) {
        const QString path = directory.filePath(ids.at(index) + QStringLiteral(".png"));
        QImage image(QSize(512, 512), QImage::Format_ARGB32_Premultiplied);
        image.fill(colors.at(index));
        QVERIFY(image.save(path));
        paths.append(path);
    }

    // Two native-size details fit beside all three baselines; a third detail must evict the
    // least-recently-used detail without removing another image's only visible fallback.
    ThumbnailCache cache(nullptr, 2200 * 1024);
    QSignalSpy readySpy(&cache, &ThumbnailCache::thumbnailReady);
    for (int index = 0; index < ids.size(); ++index) {
        cache.requestThumbnail(ids.at(index), paths.at(index), QSize(64, 64));
    }
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 3, 5000);

    cache.requestThumbnail(ids.at(0), paths.at(0), QSize(512, 512));
    cache.requestThumbnail(ids.at(1), paths.at(1), QSize(512, 512));
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 5, 5000);
    QVERIFY(!cache.thumbnail(ids.at(0), QSize(512, 512)).isNull());

    cache.requestThumbnail(ids.at(2), paths.at(2), QSize(512, 512));
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 6, 5000);

    QVERIFY(cache.hasThumbnail(ids.at(0), QSize(512, 512)));
    QVERIFY(!cache.hasThumbnail(ids.at(1), QSize(512, 512)));
    QVERIFY(cache.hasThumbnail(ids.at(2), QSize(512, 512)));
    for (const QString& id : ids) {
        QVERIFY2(!cache.thumbnail(id).isNull(), "Every image must retain a fallback pixmap.");
    }
}

void ThumbnailCacheTest::coverRenderingIsReusedUntilPaintSizeChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QImage image(QSize(800, 600), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(36, 88, 164));
    QVERIFY(image.save(path));

    CoverImageCache cache;
    const QPixmap& first = cache.pixmap(path, QSize(320, 118), 2.0);
    QVERIFY(!first.isNull());
    QCOMPARE(first.size(), QSize(640, 236));
    QCOMPARE(first.devicePixelRatio(), 2.0);
    const qint64 firstKey = first.cacheKey();

    const QPixmap& reused = cache.pixmap(path, QSize(320, 118), 2.0);
    QCOMPARE(reused.cacheKey(), firstKey);
    const QPixmap& resized = cache.pixmap(path, QSize(360, 118), 2.0);
    QCOMPARE(resized.size(), QSize(720, 236));
    QVERIFY(resized.cacheKey() != firstKey);
}

QTEST_MAIN(ThumbnailCacheTest)

#include "tst_ThumbnailCache.moc"
