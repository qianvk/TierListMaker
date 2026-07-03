#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "tier/TierProject.h"

#include <QWidget>
#include <QHash>

class QMimeData;
class QDragLeaveEvent;
class QToolButton;
class QVariantAnimation;

namespace tlm {

/** Non-scrolling drag source/drop target for unassigned imported images. */
class ImagePoolWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImagePoolWidget(QWidget* parent = nullptr);

    void setData(const TierProject* project, const AssetManager* assetManager,
                 ThumbnailCache* thumbnailCache, const QString& selectedImageId);
    int preferredOverlayHeight(int availableWidth, int availableHeight);
    QRect imageSourceRect(const QString& imageId) const;

signals:
    void imageDroppedToPool(const QString& imageId, int index);
    void imageFilesDropped(const QStringList& filePaths);
    void importRequested();
    void dragActiveChanged(bool active);
    void imageSelected(const QString& imageId);
    void imagePreviewRequested(const QString& imageId, const QRect& sourceRect);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    int visualItemCount() const;
    int tileExtentForOverlay(int availableWidth, int availableHeight) const;
    void setTileExtent(int extent);
    int visualInsertionIndexForPosition(const QPoint& position, const QString& imageId) const;
    int modelInsertionIndexForPosition(const QPoint& position, const QString& imageId) const;
    int poolItemCountExcluding(const QString& imageId) const;
    int poolIndexForImage(const QString& imageId) const;
    bool acceptsDrag(const QMimeData* mimeData) const;
    void handleDrop(const QMimeData* mimeData, const QPoint& position);
    void updateDragVisuals(const QString& imageId, int insertionIndex);
    void clearDragVisuals();
    void animateLayoutChange(const QHash<QWidget*, QRect>& oldGeometries);
    class ImageTileWidget* tileForImage(const QString& imageId) const;

    const TierProject* m_project{nullptr};
    const AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    QString m_selectedImageId;
    QWidget* m_content{nullptr};
    class FlowLayout* m_layout{nullptr};
    QToolButton* m_importButton{nullptr};
    QWidget* m_placeholder{nullptr};
    QString m_dragImageId;
    int m_placeholderIndex{-1};
    int m_tileExtent{88};
    QHash<QWidget*, QVariantAnimation*> m_geometryAnimations;
};

} // namespace tlm
