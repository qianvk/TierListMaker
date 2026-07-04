#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "tier/TierProject.h"

#include <QDialog>
#include <QMetaObject>
#include <QRect>

namespace tlm {

class GalleryGridWidget;

/** Popup gallery for every imported image, with tight square cells and shared image drag MIME. */
class ImageGalleryPopover final : public QDialog {
    Q_OBJECT

public:
    explicit ImageGalleryPopover(QWidget* parent = nullptr);

    void setData(const TierProject* project, const AssetManager* assetManager,
                 ThumbnailCache* thumbnailCache, const QString& selectedImageId);
    void setSelectedImageId(const QString& selectedImageId);
    void placeBelow(const QRect& globalAnchorRect);
    QRect imageSourceRect(const QString& imageId) const;
    QSize sizeHint() const override;

signals:
    void importRequested();
    void imageFilesDropped(const QStringList& filePaths);
    void imageSelected(const QString& imageId);
    void imagePreviewRequested(const QString& imageId, const QRect& sourceRect);
    void imageEditRequested(const QString& imageId);
    void imageRemoveRequested(const QString& imageId);
    void dragActiveChanged(bool active);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class GalleryGridWidget;

    QStringList imageIds() const;
    const TierImage* imageForId(const QString& imageId) const;
    QString resolvedPathForImage(const TierImage& image) const;
    QPixmap pixmapForImage(const QString& imageId, QSize requestedSize) const;
    QRect cellRect(int index) const;
    int cellIndexAt(const QPoint& point) const;
    void recalculateGrid(const QRect& availableGeometry);
    void requestThumbnails();

    const TierProject* m_project{nullptr};
    const AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    QString m_selectedImageId;
    GalleryGridWidget* m_grid{nullptr};
    QMetaObject::Connection m_thumbnailConnection;
    int m_tileExtent{72};
    int m_columns{1};
    int m_rows{1};
    int m_arrowCenterX{46};
};

} // namespace tlm
