#include "tier/ImagePoolWidget.h"

#include "logging/Logger.h"
#include "tier/TierDragController.h"
#include "widgets/FlowLayout.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QEvent>
#include <QFrame>
#include <QIcon>
#include <QMimeData>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace tlm {

namespace {
constexpr int kPoolPadding = 12;
constexpr int kPoolSpacing = 0;
constexpr int kPoolMinTileExtent = 56;
constexpr int kPoolPreferredTileExtent = 90;
constexpr int kPoolMaxTileExtent = 104;

// Clears old pool tiles before repopulating the pool from unassigned project images.
void clearLayout(QLayout* layout) {
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}
} // namespace

ImagePoolWidget::ImagePoolWidget(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMinimumHeight(96);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_content = new QWidget(this);
    m_content->setAcceptDrops(true);
    m_content->installEventFilter(this);
    m_content->setObjectName(QStringLiteral("ImagePoolContent"));
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_content->setStyleSheet(QStringLiteral(
        "QWidget#ImagePoolContent{background:palette(base);"
        "border:1px solid palette(mid);border-radius:18px;}"));
    m_layout = new FlowLayout(m_content, kPoolPadding, kPoolSpacing, kPoolSpacing);
    root->addWidget(m_content, 1);
}

void ImagePoolWidget::setData(const TierProject* project, const AssetManager* assetManager,
                              ThumbnailCache* thumbnailCache, const QString& selectedImageId) {
    clearDragVisuals();
    emit dragActiveChanged(false);
    m_project = project;
    m_assetManager = assetManager;
    m_thumbnailCache = thumbnailCache;
    m_selectedImageId = selectedImageId;
    rebuild();
}

int ImagePoolWidget::preferredOverlayHeight(int availableWidth, int availableHeight) {
    const int extent = tileExtentForOverlay(availableWidth, availableHeight);
    setTileExtent(extent);
    const int count = visualItemCount();
    const int contentWidth = qMax(1, availableWidth - kPoolPadding * 2);
    const int columns = qMax(1, contentWidth / qMax(1, extent));
    const int rows = qMax(1, (count + columns - 1) / columns);
    const int desired = rows * extent + (rows - 1) * kPoolSpacing + kPoolPadding * 2;
    return qBound(kPoolMinTileExtent + kPoolPadding * 2, desired,
                  qMax(kPoolMinTileExtent + kPoolPadding * 2, availableHeight - 8));
}

QRect ImagePoolWidget::imageSourceRect(const QString& imageId) const {
    if (!m_content || imageId.isEmpty()) {
        return {};
    }
    const auto tiles = m_content->findChildren<ImageTileWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (ImageTileWidget* tile : tiles) {
        if (tile && tile->imageId() == imageId) {
            return tile->imageRectIn(window());
        }
    }
    return {};
}

void ImagePoolWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (acceptsDrag(event->mimeData())) {
        Logger::debug(QStringLiteral("tier.pool.drag.enter imageMime=%1 urls=%2")
                          .arg(event->mimeData()->hasFormat(TierDragController::imageMimeType()))
                          .arg(event->mimeData()->hasUrls()));
        event->acceptProposedAction();
    }
}

void ImagePoolWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (acceptsDrag(event->mimeData())) {
        if (event->mimeData()->hasFormat(TierDragController::imageMimeType())) {
            const QString imageId = TierDragController::imageIdFromMimeData(event->mimeData());
            updateDragVisuals(imageId, visualInsertionIndexForPosition(event->position().toPoint(), imageId));
        }
        event->acceptProposedAction();
    }
}

void ImagePoolWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    clearDragVisuals();
    QWidget::dragLeaveEvent(event);
}

void ImagePoolWidget::dropEvent(QDropEvent* event) {
    handleDrop(event->mimeData(), event->position().toPoint());
    event->acceptProposedAction();
}

bool ImagePoolWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_content) {
        if (event->type() == QEvent::DragEnter) {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (acceptsDrag(dragEvent->mimeData())) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (acceptsDrag(dragEvent->mimeData())) {
                if (dragEvent->mimeData()->hasFormat(TierDragController::imageMimeType())) {
                    const QString imageId = TierDragController::imageIdFromMimeData(dragEvent->mimeData());
                    updateDragVisuals(imageId,
                                      visualInsertionIndexForPosition(mapFrom(m_content,
                                                                              dragEvent->position().toPoint()),
                                                                      imageId));
                }
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragLeave) {
            clearDragVisuals();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            handleDrop(dropEvent->mimeData(), mapFrom(m_content, dropEvent->position().toPoint()));
            dropEvent->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ImagePoolWidget::rebuild() {
    clearLayout(m_layout);
    m_importButton = nullptr;
    if (!m_project) {
        return;
    }
    for (const TierImage* image : m_project->unassignedImages()) {
        auto* tile = new ImageTileWidget(m_project, image, m_assetManager, m_thumbnailCache, m_content);
        tile->setTileExtent(m_tileExtent);
        tile->setSelected(image->id == m_selectedImageId);
        connect(tile, &ImageTileWidget::selected, this, &ImagePoolWidget::imageSelected);
        connect(tile, &ImageTileWidget::previewRequested, this, &ImagePoolWidget::imagePreviewRequested);
        connect(tile, &ImageTileWidget::dragActiveChanged, this, &ImagePoolWidget::dragActiveChanged);
        m_layout->addWidget(tile);
    }

    m_importButton = new QToolButton(m_content);
    m_importButton->setObjectName(QStringLiteral("ImagePoolImportButton"));
    m_importButton->setToolTip(tr("Import Images"));
    m_importButton->setCursor(Qt::PointingHandCursor);
    m_importButton->setIcon(QIcon(QStringLiteral(":/icons/plus.svg")));
    m_importButton->setIconSize(QSize(qRound(m_tileExtent * 0.42), qRound(m_tileExtent * 0.42)));
    m_importButton->setFixedSize(m_tileExtent, m_tileExtent);
    m_importButton->setStyleSheet(QStringLiteral(
        "QToolButton#ImagePoolImportButton{background:palette(alternate-base);"
        "border:1px dashed palette(mid);border-radius:0px;}"
        "QToolButton#ImagePoolImportButton:hover{background:palette(midlight);"
        "border-color:palette(highlight);}"
        "QToolButton#ImagePoolImportButton:pressed{background:palette(midlight);}"));
    connect(m_importButton, &QToolButton::clicked, this, [this]() {
        Logger::info(QStringLiteral("tier.pool.import.request source=importTile"));
        emit importRequested();
    });
    m_layout->addWidget(m_importButton);
}

int ImagePoolWidget::visualItemCount() const {
    return (m_project ? static_cast<int>(m_project->unassignedImages().size()) : 0) + 1;
}

int ImagePoolWidget::tileExtentForOverlay(int availableWidth, int availableHeight) const {
    const int count = qMax(1, visualItemCount());
    const int maxHeight = qMax(kPoolMinTileExtent + kPoolPadding * 2, availableHeight / 2);
    const int contentWidth = qMax(1, availableWidth - kPoolPadding * 2);
    for (int extent = kPoolMaxTileExtent; extent >= kPoolMinTileExtent; extent -= 2) {
        const int columns = qMax(1, contentWidth / extent);
        const int rows = qMax(1, (count + columns - 1) / columns);
        const int height = rows * extent + (rows - 1) * kPoolSpacing + kPoolPadding * 2;
        if (height <= maxHeight) {
            return qMin(extent, kPoolPreferredTileExtent);
        }
    }
    return kPoolMinTileExtent;
}

void ImagePoolWidget::setTileExtent(int extent) {
    extent = qBound(kPoolMinTileExtent, extent, kPoolMaxTileExtent);
    if (m_tileExtent == extent) {
        return;
    }
    m_tileExtent = extent;
    const auto tiles = m_content ? m_content->findChildren<ImageTileWidget*>(QString(), Qt::FindDirectChildrenOnly)
                                 : QList<ImageTileWidget*>();
    for (ImageTileWidget* tile : tiles) {
        if (tile) {
            tile->setTileExtent(m_tileExtent);
        }
    }
    if (m_importButton) {
        m_importButton->setFixedSize(m_tileExtent, m_tileExtent);
        m_importButton->setIconSize(QSize(qRound(m_tileExtent * 0.42), qRound(m_tileExtent * 0.42)));
    }
    if (m_placeholder) {
        m_placeholder->setFixedSize(m_tileExtent, m_tileExtent);
    }
    if (m_layout) {
        m_layout->invalidate();
    }
}

int ImagePoolWidget::visualInsertionIndexForPosition(const QPoint& position, const QString& imageId) const {
    if (!m_content || !m_layout) {
        return 0;
    }

    const QPoint local = m_content->mapFrom(this, position);
    const int itemCount = poolItemCountExcluding(imageId);
    const int perLine = qMax(1, (m_content->width() - kPoolPadding * 2) / qMax(1, m_tileExtent));
    const int totalSlots = itemCount + 1;
    const int maxLine = qMax(0, (totalSlots - 1) / perLine);
    const int line = qBound(0, (local.y() - kPoolPadding) / qMax(1, m_tileExtent), maxLine);
    int column = 0;
    if (local.x() > 0) {
        const int relativeX = local.x() - kPoolPadding;
        column = relativeX / qMax(1, m_tileExtent);
        if (relativeX - column * m_tileExtent > m_tileExtent / 2) {
            ++column;
        }
    }
    column = qBound(0, column, perLine);
    return qBound(0, line * perLine + column, itemCount);
}

int ImagePoolWidget::modelInsertionIndexForPosition(const QPoint& position, const QString& imageId) const {
    const int visualIndex = visualInsertionIndexForPosition(position, imageId);
    const int sourceIndex = poolIndexForImage(imageId);
    if (sourceIndex >= 0 && visualIndex > sourceIndex) {
        return visualIndex + 1;
    }
    return visualIndex;
}

int ImagePoolWidget::poolItemCountExcluding(const QString& imageId) const {
    if (!m_content) {
        return 0;
    }
    int count = 0;
    const auto tiles = m_content->findChildren<ImageTileWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (ImageTileWidget* tile : tiles) {
        if (tile && tile->imageId() != imageId) {
            ++count;
        }
    }
    return count;
}

int ImagePoolWidget::poolIndexForImage(const QString& imageId) const {
    if (!m_project || imageId.isEmpty()) {
        return -1;
    }
    const QVector<const TierImage*> pool = m_project->unassignedImages();
    for (int index = 0; index < pool.size(); ++index) {
        if (pool.at(index) && pool.at(index)->id == imageId) {
            return index;
        }
    }
    return -1;
}

bool ImagePoolWidget::acceptsDrag(const QMimeData* mimeData) const {
    return mimeData && (mimeData->hasFormat(TierDragController::imageMimeType()) || mimeData->hasUrls());
}

void ImagePoolWidget::handleDrop(const QMimeData* mimeData, const QPoint& position) {
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        if (!paths.isEmpty()) {
            Logger::info(QStringLiteral("tier.pool.files.drop count=%1").arg(paths.size()));
            emit imageFilesDropped(paths);
        }
        return;
    }

    const QString imageId = TierDragController::imageIdFromMimeData(mimeData);
    if (!imageId.isEmpty()) {
        const int visualIndex = visualInsertionIndexForPosition(position, imageId);
        const int insertionIndex = modelInsertionIndexForPosition(position, imageId);
        Logger::info(QStringLiteral("tier.pool.image.drop imageId=%1 visualIndex=%2 modelIndex=%3 sourceIndex=%4")
                         .arg(imageId)
                         .arg(visualIndex)
                         .arg(insertionIndex)
                         .arg(poolIndexForImage(imageId)));
        clearDragVisuals();
        emit imageDroppedToPool(imageId, insertionIndex);
    }
}

void ImagePoolWidget::updateDragVisuals(const QString& imageId, int insertionIndex) {
    if (!m_layout || !m_content || imageId.isEmpty()) {
        return;
    }

    QHash<QWidget*, QRect> oldGeometries;
    const auto widgets = m_content->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : widgets) {
        if (widget && widget->isVisible()) {
            oldGeometries.insert(widget, widget->geometry());
        }
    }

    if (!m_placeholder) {
        auto* placeholder = new QFrame(m_content);
        placeholder->setObjectName(QStringLiteral("ImagePoolDragPlaceholder"));
        placeholder->setFixedSize(m_tileExtent, m_tileExtent);
        placeholder->setStyleSheet(QStringLiteral(
            "QFrame#ImagePoolDragPlaceholder{background:rgba(22,119,255,42);border:none;}"));
        m_placeholder = placeholder;
    }

    if (m_dragImageId != imageId) {
        if (ImageTileWidget* previous = tileForImage(m_dragImageId)) {
            previous->show();
        }
        m_dragImageId = imageId;
    }
    if (ImageTileWidget* source = tileForImage(imageId)) {
        source->hide();
    }

    insertionIndex = qBound(0, insertionIndex, poolItemCountExcluding(imageId));
    if (m_placeholderIndex != insertionIndex || !m_placeholder->isVisible()) {
        int layoutIndex = m_layout->count();
        int visibleIndex = 0;
        for (int i = 0; i < m_layout->count(); ++i) {
            QLayoutItem* item = m_layout->itemAt(i);
            if (!item || item->widget() == m_placeholder || item->isEmpty()) {
                continue;
            }
            if (item->widget() == m_importButton) {
                if (visibleIndex >= insertionIndex) {
                    layoutIndex = i;
                    break;
                }
                continue;
            }
            if (visibleIndex >= insertionIndex) {
                layoutIndex = i;
                break;
            }
            ++visibleIndex;
        }
        m_layout->moveWidget(m_placeholder, layoutIndex);
        m_placeholder->show();
        m_placeholderIndex = insertionIndex;
        m_layout->activate();
        animateLayoutChange(oldGeometries);
        Logger::debug(QStringLiteral("tier.pool.image.drag.intent imageId=%1 index=%2")
                          .arg(imageId)
                          .arg(insertionIndex));
    }
}

void ImagePoolWidget::clearDragVisuals() {
    for (QVariantAnimation* animation : std::as_const(m_geometryAnimations)) {
        if (animation) {
            animation->stop();
            animation->deleteLater();
        }
    }
    m_geometryAnimations.clear();

    if (ImageTileWidget* source = tileForImage(m_dragImageId)) {
        source->show();
    }
    m_dragImageId.clear();
    m_placeholderIndex = -1;

    if (m_placeholder) {
        for (int i = 0; i < m_layout->count(); ++i) {
            QLayoutItem* item = m_layout->itemAt(i);
            if (item && item->widget() == m_placeholder) {
                delete m_layout->takeAt(i);
                break;
            }
        }
        m_placeholder->hide();
        m_placeholder->deleteLater();
        m_placeholder = nullptr;
        if (m_layout) {
            m_layout->activate();
        }
    }
}

void ImagePoolWidget::animateLayoutChange(const QHash<QWidget*, QRect>& oldGeometries) {
    const auto widgets = m_content->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : widgets) {
        if (!widget || !widget->isVisible()) {
            continue;
        }
        const QRect target = widget->geometry();
        const QRect start = oldGeometries.value(widget, target);
        if (start == target) {
            continue;
        }
        if (QVariantAnimation* existing = m_geometryAnimations.take(widget)) {
            existing->stop();
            existing->deleteLater();
        }
        widget->setGeometry(start);
        auto* animation = new QVariantAnimation(this);
        m_geometryAnimations.insert(widget, animation);
        animation->setDuration(180);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(start);
        animation->setEndValue(target);
        connect(animation, &QVariantAnimation::valueChanged, widget, [widget](const QVariant& value) {
            widget->setGeometry(value.toRect());
        });
        connect(animation, &QVariantAnimation::finished, this, [this, widget, animation, target]() {
            if (m_geometryAnimations.value(widget) == animation) {
                m_geometryAnimations.remove(widget);
            }
            if (widget) {
                widget->setGeometry(target);
            }
            animation->deleteLater();
        });
        animation->start();
    }
}

ImageTileWidget* ImagePoolWidget::tileForImage(const QString& imageId) const {
    if (!m_content || imageId.isEmpty()) {
        return nullptr;
    }
    const auto tiles = m_content->findChildren<ImageTileWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (ImageTileWidget* tile : tiles) {
        if (tile && tile->imageId() == imageId) {
            return tile;
        }
    }
    return nullptr;
}

} // namespace tlm
