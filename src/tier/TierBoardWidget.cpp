#include "tier/TierBoardWidget.h"

#include "tier/TierListDelegate.h"
#include "tier/TierListModel.h"
#include "tier/TierListView.h"

#include <QAbstractItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace tlm {

TierBoardWidget::TierBoardWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_model = new TierListModel(this);
    m_delegate = new TierListDelegate(this);
    m_view = new TierListView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    layout->addWidget(m_view, 1);

    connect(m_model, &QAbstractItemModel::modelReset, m_view, &TierListView::refreshLayoutMetrics);
    connect(m_view, &TierListView::imageDropped, this, &TierBoardWidget::imageDropped);
    connect(m_view, &TierListView::imageSelected, this, &TierBoardWidget::imageSelected);
    connect(m_view, &TierListView::imagePreviewRequested, this, &TierBoardWidget::imagePreviewRequested);
    connect(m_view, &TierListView::galleryMissionControlRequested, this,
            &TierBoardWidget::galleryMissionControlRequested);
    connect(m_view, &TierListView::imageEditRequested, this, &TierBoardWidget::imageEditRequested);
    connect(m_view, &TierListView::imageRemoveFromTierRowRequested, this,
            &TierBoardWidget::imageRemoveFromTierRowRequested);
    connect(m_view, &TierListView::imageRemoveFromGalleryRequested, this,
            &TierBoardWidget::imageRemoveFromGalleryRequested);
    connect(m_view, &TierListView::rowEditRequested, this, &TierBoardWidget::rowEditRequested);
    connect(m_view, &TierListView::rowClearRequested, this, &TierBoardWidget::rowClearRequested);
    connect(m_view, &TierListView::rowDeleteRequested, this, &TierBoardWidget::rowDeleteRequested);
    connect(m_view, &TierListView::rowInsertAboveRequested, this,
            &TierBoardWidget::rowInsertAboveRequested);
    connect(m_view, &TierListView::rowInsertBelowRequested, this,
            &TierBoardWidget::rowInsertBelowRequested);
    connect(m_view, &TierListView::rowMovedToIndex, this, &TierBoardWidget::rowMovedToIndex);
}

void TierBoardWidget::setData(const TierProject* project, const AssetManager* assetManager,
                              ThumbnailCache* thumbnailCache, const QString& selectedImageId) {
    m_project = project;
    m_assetManager = assetManager;
    m_thumbnailCache = thumbnailCache;
    m_selectedImageId = selectedImageId;

    if (m_thumbnailConnection) {
        disconnect(m_thumbnailConnection);
        m_thumbnailConnection = {};
    }
    if (m_thumbnailCache) {
        m_thumbnailConnection = connect(m_thumbnailCache, &ThumbnailCache::thumbnailReady, m_view->viewport(),
                                        [this](const QString& imageId) {
                                            if (m_view) {
                                                m_view->updateImageVisual(imageId);
                                            }
                                        });
    }

    m_delegate->setContext(m_project, m_assetManager, m_thumbnailCache, m_selectedImageId);
    m_model->setProject(m_project);
    m_view->refreshLayoutMetrics();
    QTimer::singleShot(0, m_view, &TierListView::refreshLayoutMetrics);
}

void TierBoardWidget::setSelectedImageId(const QString& selectedImageId) {
    if (m_selectedImageId == selectedImageId) {
        return;
    }
    m_selectedImageId = selectedImageId;
    if (m_delegate) {
        m_delegate->setSelectedImageId(m_selectedImageId);
    }
    refreshVisuals();
}

void TierBoardWidget::refreshVisuals() {
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

QRect TierBoardWidget::imageSourceRect(const QString& imageId) const {
    return m_view ? m_view->imageSourceRect(imageId) : QRect();
}

void TierBoardWidget::toggleMissionControlMode() {
    if (m_view) {
        m_view->toggleMissionControlActive();
    }
}

void TierBoardWidget::toggleGalleryMissionControlMode() {
    if (m_view) {
        m_view->toggleGalleryMissionControlActive();
    }
}

void TierBoardWidget::setMissionControlMode(bool active) {
    if (m_view) {
        m_view->setMissionControlActive(active);
    }
}

bool TierBoardWidget::isMissionControlModeActive() const {
    return m_view && m_view->isMissionControlActive();
}

void TierBoardWidget::setBlankAreaActions(BlankAreaAction doubleClickAction,
                                          BlankAreaAction longPressAction) {
    if (m_view) {
        m_view->setBlankAreaActions(doubleClickAction, longPressAction);
    }
}

void TierBoardWidget::setTierFocusMode(bool enabled) {
    if (m_view) {
        m_view->setTierFocusMode(enabled);
    }
}

void TierBoardWidget::setToolTipsEnabled(bool enabled) {
    if (m_view) {
        m_view->setToolTipsEnabled(enabled);
    }
}

} // namespace tlm
