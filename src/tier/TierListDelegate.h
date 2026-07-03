#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "tier/TierProject.h"

#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QStyledItemDelegate>
#include <QVector>

namespace tlm {

/** Paints table-like tier rows and exposes geometry helpers for view hit testing. */
class TierListDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit TierListDelegate(QObject* parent = nullptr);

    void setContext(const TierProject* project, const AssetManager* assetManager,
                    ThumbnailCache* thumbnailCache, QString selectedImageId);

    const TierProject* project() const { return m_project; }
    int labelWidth() const;
    static int minimumLabelWidth();
    static int outerRadius();
    static int rowUnitsForImageCount(int imageCount, int viewportWidth);
    static int rowUnitsForImageCount(int imageCount, int viewportWidth, int lineHeight);
    static int rowUnitsForImageCount(int imageCount, int viewportWidth, int lineHeight,
                                     int labelWidth);

    QRect labelRect(const QRect& rowRect) const;
    QVector<QRect> tileRects(const QModelIndex& index, const QRect& rowRect) const;
    QVector<QRect> tileRectsForCount(const QModelIndex& index, const QRect& rowRect, int itemCount) const;
    QRect tileImageRect(const QRect& tileRect) const;
    QString imageIdAt(const QModelIndex& index, const QRect& rowRect, const QPoint& point) const;
    QRect imageRectForId(const QModelIndex& index, const QRect& rowRect, const QString& imageId) const;
    QPixmap pixmapForImageId(const QString& imageId) const;
    QPixmap fullPixmapForImageId(const QString& imageId) const;
    QSize sourceSizeForImageId(const QString& imageId) const;
    int insertionIndexForPosition(const QModelIndex& index, const QRect& rowRect,
                                  const QPoint& point) const;
    int insertionIndexForPosition(const QModelIndex& index, const QRect& rowRect,
                                  const QPoint& point, int itemCount) const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    QStringList imageIdsForIndex(const QModelIndex& index) const;
    QPixmap pixmapForImage(const TierImage& image) const;

    const TierProject* m_project{nullptr};
    const AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    QString m_selectedImageId;
};

} // namespace tlm
