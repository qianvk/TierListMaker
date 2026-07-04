#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "settings/AppSettings.h"
#include "tier/TierProject.h"

#include <QMetaObject>
#include <QWidget>

namespace tlm {

class TierListDelegate;
class TierListModel;
class TierListView;

/** Adaptive non-scrolling model/view tier board. */
class TierBoardWidget : public QWidget {
    Q_OBJECT

public:
    explicit TierBoardWidget(QWidget* parent = nullptr);

    void setData(const TierProject* project, const AssetManager* assetManager,
                 ThumbnailCache* thumbnailCache, const QString& selectedImageId);
    void setSelectedImageId(const QString& selectedImageId);
    void refreshVisuals();
    QRect imageSourceRect(const QString& imageId) const;
    void toggleMissionControlMode();
    void toggleGalleryMissionControlMode(const QRect& sourceGlobalRect);
    void setMissionControlMode(bool active);
    bool isMissionControlModeActive() const;
    void setBlankAreaActions(BlankAreaAction doubleClickAction, BlankAreaAction longPressAction);

signals:
    void imageDropped(const QString& imageId, const QString& rowId, int index);
    void imageSelected(const QString& imageId);
    void imagePreviewRequested(const QString& imageId, const QRect& sourceRect);
    void galleryMissionControlRequested();
    void imageEditRequested(const QString& imageId);
    void imageRemoveFromTierRowRequested(const QString& imageId);
    void imageRemoveFromGalleryRequested(const QString& imageId);
    void rowEditRequested(const QString& rowId);
    void rowMovedToIndex(const QString& rowId, int destinationIndex);

private:
    const TierProject* m_project{nullptr};
    const AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    QString m_selectedImageId;
    TierListModel* m_model{nullptr};
    TierListDelegate* m_delegate{nullptr};
    TierListView* m_view{nullptr};
    QMetaObject::Connection m_thumbnailConnection;
};

} // namespace tlm
