#pragma once

#include "settings/AppSettings.h"

#include <QListView>
#include <QPersistentModelIndex>
#include <QHash>
#include <QPointF>
#include <QPixmap>
#include <QRectF>
#include <QStringList>
#include <QVector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QContextMenuEvent;
class QDropEvent;
class QEvent;
class QKeyEvent;
class QMimeData;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QVariantAnimation;

namespace tlm {

class TierListDelegate;
class TierListModel;

/** List view owning tier row DnD, image DnD, and fixed-height board layout. */
class TierListView : public QListView {
    Q_OBJECT

public:
    struct MissionTile {
        QString imageId;
        QRectF rect;
        QSize sourceSize;
    };

    explicit TierListView(QWidget* parent = nullptr);

    void refreshLayoutMetrics();
    qreal visualOffsetForIndex(const QModelIndex& index) const;
    bool isReorderSourceIndex(const QModelIndex& index) const;
    bool isVisualFirstRow(const QModelIndex& index) const;
    bool isVisualLastRow(const QModelIndex& index) const;
    bool isImageDragSource(const QString& imageId) const;
    QPointF visualOffsetForImage(const QString& imageId) const;
    QRect imageSourceRect(const QString& imageId) const;
    qreal dockScaleForImage(const QModelIndex& index, const QRect& tileRect, const QString& imageId) const;
    QPointF dockOffsetForImage(const QModelIndex& index, const QRect& tileRect, const QString& imageId) const;
    bool isMissionControlActive() const { return m_missionControlActive; }
    qreal missionTransitionProgress() const { return m_missionTransitionProgress; }
    bool isGalleryMissionLayerVisible() const {
        return m_missionFromGallery && (m_missionControlActive || m_missionTransitionProgress > 0.001);
    }
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

public slots:
    void setMissionControlActive(bool active);
    void setGalleryMissionControlActive(bool active, const QRect& sourceGlobalRect);
    void toggleMissionControlActive();
    void toggleGalleryMissionControlActive(const QRect& sourceGlobalRect);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class PressKind {
        None,
        RowLabel,
        ImageTile,
        BlankArea
    };

    TierListModel* tierModel() const;
    TierListDelegate* tierDelegate() const;

    void resetPressState();
    void beginRowReorderVisuals(const QModelIndex& index);
    void finishRowReorderVisuals(bool accepted);
    void animateReorderToIndex(int destinationIndex);
    void animateRowOffset(int row, qreal targetOffset);
    void animatePlaceholder(qreal targetY);
    QHash<int, qreal> targetOffsetsForDestination(int destinationIndex) const;
    qreal placeholderYForDestination(int destinationIndex) const;
    void stopReorderAnimations();
    void beginImageDragVisuals(const QString& imageId, bool synchronousFeedback = false);
    void finishImageDragVisuals();
    void updateImageDropIntentAt(const QPoint& viewportPoint);
    void updateImageDropIntent(const QModelIndex& target, int insertionIndex);
    void applyImagePreviewTargetRow(int targetRow);
    int previewImageCountForRow(int row) const;
    int imageInsertionIndexForPosition(const QModelIndex& target, const QPoint& point) const;
    void animateImageOffset(const QString& imageId, const QPointF& targetOffset);
    void animateImagePlaceholder(const QRectF& targetRect);
    void stopImageAnimations();
    QHash<QString, QPointF> targetImageOffsets(const QModelIndex& target, int insertionIndex,
                                               QRectF* placeholderRect) const;
    void startRowDrag();
    void startImageDrag();
    QWidget* createRowDragCatchment();
    QRect rowDragCatchmentGeometry(QWidget* host) const;
    QPoint viewportPointFromGlobal(const QPoint& globalPoint) const;
    void updateRowDropIntent(const QPoint& viewportPoint, const QString& rowId);
    bool commitRowDrop(const QString& rowId, int destinationIndex, const char* reason);
    void clearDropState();
    void clearImageDropState();
    void updateDockHover(const QPoint& viewportPoint);
    void animateDockHover(qreal targetProgress);
    void stopDockHoverAnimation();
    void animateMissionTransition(qreal targetProgress);
    void stopMissionTransitionAnimation();
    void updateMissionHover(const QPoint& viewportPoint);
    void animateMissionHover(qreal targetProgress);
    void stopMissionHoverAnimation();
    void scheduleMissionImageLift(const QString& imageId, const QPoint& viewportPoint);
    void startMissionImageLiftDrag(const QString& imageId, const QPoint& viewportPoint, int pressSerial);
    void scheduleBlankMissionControl(const QPoint& viewportPoint);
    bool runBlankAreaAction(BlankAreaAction action, const char* trigger, const QPoint& viewportPoint);
    void updateBlankAreaToolTip(const QPoint& viewportPoint);
    QString blankAreaHintText() const;
    QRect tierRowImageRectForLift(const QString& imageId) const;
    void completeMissionExitForLift();
    void invalidateMissionControlLayout() const;
    void ensureMissionControlLayout() const;
    QStringList missionImageIds() const;
    QString missionImageAt(const QPoint& viewportPoint) const;
    QRect missionImageRect(const QString& imageId) const;
    void paintMissionControl(QPainter* painter);
    QHash<QString, QRectF> normalImageRects() const;
    QRectF interpolatedMissionRect(const QString& imageId, const QRectF& targetRect) const;
    QRectF galleryMissionSourceRect() const;
    QRectF galleryMissionCenterRect(const QRectF& targetRect) const;
    QVector<MissionTile> missionDisplayTiles() const;
    QPixmap missionPixmapForImage(const QString& imageId, QSize targetPixelSize, bool fullQuality);
    int rowDropIndexForPosition(const QPoint& point, const QString& rowId) const;
    QModelIndex imageDropIndexForPosition(const QPoint& point) const;
    QRect animatedVisualRect(const QModelIndex& index) const;
    QModelIndex animatedIndexAt(const QPoint& point) const;
    QRect viewportImageRect(const QModelIndex& index, const QString& imageId) const;
    bool acceptsTierDrag(const QMimeData* mimeData) const;
    void paintCanvasBackground(QPainter* painter);
    QString resolvedCanvasBackgroundPath() const;
    QPixmap canvasBackgroundPixmap(const QString& path);

    PressKind m_pressKind{PressKind::None};
    QPoint m_pressPosition;
    QPersistentModelIndex m_pressedIndex;
    QString m_pressedRowId;
    QString m_pressedImageId;
    QString m_activeImageId;
    QPersistentModelIndex m_activeImageIndex;

    bool m_rowDropActive{false};
    bool m_rowDragCommitted{false};
    int m_reorderSourceRow{-1};
    QRect m_reorderSourceRect;
    qreal m_placeholderY{0.0};
    QHash<int, qreal> m_reorderRowOffsets;
    QHash<int, QVariantAnimation*> m_reorderRowAnimations;
    QVariantAnimation* m_placeholderAnimation{nullptr};
    QString m_rowDropId;
    int m_rowDropIndex{-1};
    QPersistentModelIndex m_imageDropIndex;

    bool m_imageDragActive{false};
    QString m_imageDragId;
    int m_imageDragSourceRow{-1};
    int m_imagePreviewTargetRow{-1};
    int m_imageDropInsertionIndex{-1};
    QRectF m_imagePlaceholderRect;
    QHash<QString, QPointF> m_imageTileOffsets;
    QHash<QString, QVariantAnimation*> m_imageTileAnimations;
    QHash<QString, QRectF> m_imageDragOriginalRects;
    QVariantAnimation* m_imagePlaceholderAnimation{nullptr};
    bool m_imageDragSynchronousFeedback{false};

    QString m_dockHoverImageId;
    int m_dockHoverRow{-1};
    QPointF m_dockHoverPosition;
    qreal m_dockHoverProgress{0.0};
    QVariantAnimation* m_dockHoverAnimation{nullptr};
    QString m_canvasBackgroundCachePath;
    QPixmap m_canvasBackgroundCache;
    bool m_missionControlActive{false};
    bool m_missionFromGallery{false};
    qreal m_missionTransitionProgress{0.0};
    QVariantAnimation* m_missionTransitionAnimation{nullptr};
    QHash<QString, QRectF> m_missionNormalRects;
    QRectF m_missionGallerySourceRect;
    QString m_missionHoverImageId;
    QString m_missionLiftImageId;
    QPointF m_missionHoverPosition;
    qreal m_missionHoverProgress{0.0};
    QVariantAnimation* m_missionHoverAnimation{nullptr};
    int m_missionPressSerial{0};
    int m_blankPressSerial{0};
    bool m_suppressBlankDoubleClick{false};
    BlankAreaAction m_blankDoubleClickAction{BlankAreaAction::GalleryMissionControl};
    BlankAreaAction m_blankLongPressAction{BlankAreaAction::TierMissionControl};
    mutable bool m_missionLayoutDirty{true};
    mutable QSize m_missionLayoutViewportSize;
    mutable QStringList m_missionLayoutImageIds;
    mutable QVector<MissionTile> m_missionTiles;
    mutable QHash<QString, QSize> m_missionSourceSizeCache;
};

} // namespace tlm
