#include "tier/ImageGalleryPopover.h"

#include "logging/Logger.h"
#include "theme/Theme.h"
#include "tier/TierDragController.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/overlays/VkPopover.h>

namespace tlm {

namespace {
constexpr int kMinimumTileExtent = 44;
constexpr int kPreferredTileExtent = 72;
constexpr int kMaximumTileExtent = 86;

int platformPopoverRadius() {
    return std::max(0, qRound(vkui::VkThemeManager::instance()
                                  ->theme()
                                  .metrics()
                                  .popoverCornerRadius));
}

QRect centeredCropSourceRect(const QPixmap& pixmap, const QSize& targetSize) {
    if (pixmap.isNull() || targetSize.isEmpty()) {
        return {};
    }

    const QSize sourceSize = pixmap.size();
    const qreal targetRatio = static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
    const qreal sourceRatio = static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
    if (sourceRatio > targetRatio) {
        const int cropWidth = qRound(sourceSize.height() * targetRatio);
        return QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
    }

    const int cropHeight = qRound(sourceSize.width() / targetRatio);
    return QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(), cropHeight);
}

QPixmap squareDragPixmap(const QPixmap& pixmap, const QSize& logicalSize, qreal devicePixelRatio,
                         const QRect& sourceRect = {}) {
    if (pixmap.isNull() || logicalSize.isEmpty()) {
        return {};
    }

    QPixmap result(logicalSize * devicePixelRatio);
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.drawPixmap(QRect(QPoint(0, 0), logicalSize), pixmap,
                       sourceRect.isValid() ? sourceRect
                                            : centeredCropSourceRect(pixmap, logicalSize));
    return result;
}

QStringList filePathsFromMimeData(const QMimeData* mimeData) {
    QStringList paths;
    if (!mimeData || !mimeData->hasUrls()) {
        return paths;
    }
    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    return paths;
}
} // namespace

class GalleryGridWidget final : public QWidget {
public:
    explicit GalleryGridWidget(ImageGalleryPopover* owner) : QWidget(owner), m_owner(owner) {
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

    QRect imageSourceRect(const QString& imageId) const {
        if (!m_owner || imageId.isEmpty()) {
            return {};
        }
        const QStringList ids = m_owner->imageIds();
        const int index = static_cast<int>(ids.indexOf(imageId));
        if (index < 0) {
            return {};
        }
        const QRect localRect = m_owner->cellRect(index);
        const QPoint globalTopLeft = mapToGlobal(localRect.topLeft());
        QWidget* host = m_owner->m_hostWindow.data();
        return host ? QRect(host->mapFromGlobal(globalTopLeft), localRect.size())
                    : QRect(globalTopLeft, localRect.size());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_owner) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), platformPopoverRadius(),
                            platformPopoverRadius());
        painter.setClipPath(clip);

        painter.fillPath(clip, activeThemeTokens().popoverBackground);

        const QStringList ids = m_owner->imageIds();
        const int itemCount = static_cast<int>(ids.size()) + 1;
        for (int index = 0; index < itemCount; ++index) {
            const QRect cell = m_owner->cellRect(index);
            if (!cell.isValid() || !cell.intersects(rect())) {
                continue;
            }

            if (index < ids.size()) {
                paintImageCell(&painter, cell, ids.at(index));
            } else {
                paintImportCell(&painter, cell);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !m_owner) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_pressPosition = event->pos();
        m_pressedIndex = m_owner->cellIndexAt(event->pos());
        m_dragging = false;
        setFocus(Qt::MouseFocusReason);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_owner || !(event->buttons() & Qt::LeftButton) || m_pressedIndex < 0) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        if (m_dragging || (event->pos() - m_pressPosition).manhattanLength() <
                              QApplication::startDragDistance()) {
            return;
        }

        const QStringList ids = m_owner->imageIds();
        if (m_pressedIndex >= ids.size()) {
            return;
        }

        const QString imageId = ids.at(m_pressedIndex);
        const QRect sourceRect = m_owner->cellRect(m_pressedIndex);
        QPixmap pixmap = m_owner->pixmapForImage(imageId, sourceRect.size() * 2);
        const TierImage* image = m_owner->imageForId(imageId);
        auto* drag = new QDrag(this);
        drag->setMimeData(TierDragController::createMimeData(imageId));
        if (!pixmap.isNull()) {
            const QRect crop =
                image ? image->thumbnailSourceRect(pixmap.size(), sourceRect.size()) : QRect();
            drag->setPixmap(squareDragPixmap(pixmap, sourceRect.size(), devicePixelRatioF(), crop));
            drag->setHotSpot(event->pos() - sourceRect.topLeft());
        }

        m_dragging = true;
        QPointer<ImageGalleryPopover> guard(m_owner);
        emit m_owner->dragActiveChanged(true);
        Logger::info(QStringLiteral("tier.gallery.image.drag.start imageId=%1").arg(imageId));
        const Qt::DropAction result = drag->exec(Qt::MoveAction);
        if (guard) {
            emit m_owner->dragActiveChanged(false);
            Logger::info(QStringLiteral("tier.gallery.image.drag.finish imageId=%1 result=%2")
                             .arg(imageId)
                             .arg(static_cast<int>(result)));
        }
        m_dragging = false;
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!m_owner || event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const int releasedIndex = m_owner->cellIndexAt(event->pos());
        const QStringList ids = m_owner->imageIds();
        if (!m_dragging && releasedIndex == m_pressedIndex && releasedIndex >= 0) {
            if (releasedIndex >= ids.size()) {
                Logger::info(QStringLiteral("tier.gallery.import.request"));
                emit m_owner->importRequested();
            } else {
                const QString imageId = ids.at(releasedIndex);
                Logger::debug(QStringLiteral("tier.gallery.image.select imageId=%1").arg(imageId));
                emit m_owner->imageSelected(imageId);
            }
        }
        m_pressedIndex = -1;
        m_dragging = false;
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (!m_owner || event->button() != Qt::LeftButton) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }
        const QStringList ids = m_owner->imageIds();
        const int index = m_owner->cellIndexAt(event->pos());
        if (index >= 0 && index < ids.size()) {
            const QString imageId = ids.at(index);
            const QRect source = imageSourceRect(imageId);
            Logger::info(
                QStringLiteral("tier.gallery.preview.request source=double-click imageId=%1")
                    .arg(imageId));
            emit m_owner->imagePreviewRequested(imageId, source);
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (!m_owner) {
            QWidget::contextMenuEvent(event);
            return;
        }

        const QStringList ids = m_owner->imageIds();
        const int index = m_owner->cellIndexAt(event->pos());
        if (index < 0 || index >= ids.size()) {
            QWidget::contextMenuEvent(event);
            return;
        }

        const QString imageId = ids.at(index);
        emit m_owner->imageSelected(imageId);
        QMenu menu(this);
        QAction* editAction = menu.addAction(vkui::icon(vkui::VkSymbol::Edit), tr("Edit"));
        QAction* removeAction =
            menu.addAction(vkui::icon(vkui::VkSymbol::Trash, vkui::VkIconRole::Destructive),
                           tr("Remove from Image Gallery"));
        QPointer<ImageGalleryPopover> guard(m_owner);
        m_owner->setOutsideDismissSuspended(true);
        QAction* chosen = menu.exec(event->globalPos());
        if (guard) {
            guard->setOutsideDismissSuspended(false);
        }
        if (!guard) {
            event->accept();
            return;
        }
        if (chosen == editAction) {
            Logger::info(QStringLiteral("tier.gallery.context.edit imageId=%1").arg(imageId));
            emit m_owner->imageEditRequested(imageId);
        } else if (chosen == removeAction) {
            Logger::info(QStringLiteral("tier.gallery.context.remove imageId=%1").arg(imageId));
            emit m_owner->imageRemoveRequested(imageId);
        }
        event->accept();
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!filePathsFromMimeData(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
            return;
        }
        QWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (!filePathsFromMimeData(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
            return;
        }
        QWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (!m_owner) {
            QWidget::dropEvent(event);
            return;
        }
        const QStringList paths = filePathsFromMimeData(event->mimeData());
        if (paths.isEmpty()) {
            QWidget::dropEvent(event);
            return;
        }
        Logger::info(QStringLiteral("tier.gallery.files.drop count=%1").arg(paths.size()));
        emit m_owner->imageFilesDropped(paths);
        event->acceptProposedAction();
    }

private:
    void paintImageCell(QPainter* painter, const QRect& cell, const QString& imageId) {
        const ThemeTokens& colors = activeThemeTokens();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(cell, colors.elevatedBackground);
        const QPixmap pixmap = m_owner->pixmapForImage(imageId, cell.size() * 2);
        if (!pixmap.isNull()) {
            const TierImage* image = m_owner->imageForId(imageId);
            painter->drawPixmap(cell, pixmap,
                                image ? image->thumbnailSourceRect(pixmap.size(), cell.size())
                                      : centeredCropSourceRect(pixmap, cell.size()));
        }
        if (imageId == m_owner->m_selectedImageId) {
            painter->setPen(QPen(colors.accent, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cell.adjusted(1, 1, -2, -2));
        }
        painter->restore();
    }

    void paintImportCell(QPainter* painter, const QRect& cell) {
        const ThemeTokens& colors = activeThemeTokens();
        painter->save();
        painter->fillRect(cell, colors.controlFillHovered);
        const QColor stroke = colors.symbolSecondary;
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(stroke, qMax(2, cell.width() / 28), Qt::SolidLine, Qt::RoundCap));
        const int arm = qMax(11, cell.width() / 5);
        const QPoint center = cell.center();
        painter->drawLine(QPoint(center.x() - arm, center.y()),
                          QPoint(center.x() + arm, center.y()));
        painter->drawLine(QPoint(center.x(), center.y() - arm),
                          QPoint(center.x(), center.y() + arm));
        painter->restore();
    }

    ImageGalleryPopover* m_owner{nullptr};
    QPoint m_pressPosition;
    int m_pressedIndex{-1};
    bool m_dragging{false};
};

ImageGalleryPopover::ImageGalleryPopover(QWidget* parent)
    : QWidget(nullptr), m_grid(new GalleryGridWidget(this)), m_popover(new vkui::VkPopover(parent)),
      m_hostWindow(parent ? parent->window() : nullptr) {
    setObjectName(QStringLiteral("ImageGalleryPopover"));
    m_popover->setPreferredPlacement(vkui::VkPopoverPlacement::Below);
    m_popover->setContentWidget(this);
}

void ImageGalleryPopover::setData(const TierProject* project, const AssetManager* assetManager,
                                  ThumbnailCache* thumbnailCache, const QString& selectedImageId) {
    m_project = project;
    m_assetManager = assetManager;
    if (m_thumbnailConnection) {
        disconnect(m_thumbnailConnection);
        m_thumbnailConnection = {};
    }
    m_thumbnailCache = thumbnailCache;
    m_selectedImageId = selectedImageId;
    if (m_thumbnailCache && m_grid) {
        m_thumbnailConnection = connect(m_thumbnailCache, &ThumbnailCache::thumbnailReady, m_grid,
                                        [this](const QString&) { m_grid->update(); });
    }
    requestThumbnails();
    if (isOpen()) {
        QScreen* screen = m_anchor && m_anchor->screen() ? m_anchor->screen() : nullptr;
        const QRect available = screen ? screen->availableGeometry().adjusted(10, 10, -10, -10)
                                       : QRect(QPoint(10, 10), QSize(820, 620));
        recalculateGrid(available);
        resize(sizeHint());
        if (m_grid) {
            m_grid->setGeometry(rect());
        }
    }
    if (m_grid) {
        m_grid->update();
    }
}

void ImageGalleryPopover::setSelectedImageId(const QString& selectedImageId) {
    if (m_selectedImageId == selectedImageId) {
        return;
    }
    m_selectedImageId = selectedImageId;
    if (m_grid) {
        m_grid->update();
    }
}

void ImageGalleryPopover::setOutsideDismissSuspended(bool suspended) {
    if (m_outsideDismissSuspended == suspended) {
        return;
    }
    m_outsideDismissSuspended = suspended;
    if (m_popover) {
        const auto normalPolicy = vkui::VkPopoverClosePolicyFlag::OutsideClick |
                                  vkui::VkPopoverClosePolicyFlag::EscapeKey |
                                  vkui::VkPopoverClosePolicyFlag::AnchorDestroyed |
                                  vkui::VkPopoverClosePolicyFlag::WindowDeactivated;
        m_popover->setClosePolicy(suspended ? vkui::VkPopoverClosePolicyFlag::AnchorDestroyed
                                            : normalPolicy);
    }
    Logger::debug(QStringLiteral("tier.gallery.popover.dismiss.suspended value=%1").arg(suspended));
}

void ImageGalleryPopover::openFor(QWidget* anchor) {
    if (!anchor || !m_popover) {
        return;
    }
    m_anchor = anchor;
    QScreen* screen = anchor->screen();
    const QRect available = screen ? screen->availableGeometry().adjusted(10, 10, -10, -10)
                                   : QRect(QPoint(10, 10), QSize(820, 620));
    recalculateGrid(available);
    resize(sizeHint());
    if (m_grid) {
        m_grid->setGeometry(rect());
    }
    m_popover->openFor(anchor);
    Logger::debug(QStringLiteral("tier.gallery.popover.place images=%1 columns=%2 rows=%3 tile=%4")
                      .arg(imageIds().size())
                      .arg(m_columns)
                      .arg(m_rows)
                      .arg(m_tileExtent));
}

void ImageGalleryPopover::closeAnimated() {
    if (m_popover) {
        m_popover->closeAnimated();
    }
}

void ImageGalleryPopover::closeImmediately() {
    if (m_popover) {
        m_popover->closeImmediately();
    }
}

bool ImageGalleryPopover::isOpen() const {
    return m_popover && m_popover->isOpen();
}

QRect ImageGalleryPopover::imageSourceRect(const QString& imageId) const {
    return m_grid ? m_grid->imageSourceRect(imageId) : QRect();
}

QSize ImageGalleryPopover::sizeHint() const {
    return QSize(qMax(1, m_columns) * m_tileExtent, qMax(1, m_rows) * m_tileExtent);
}

void ImageGalleryPopover::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_grid) {
        m_grid->setGeometry(rect());
    }
}

QStringList ImageGalleryPopover::imageIds() const {
    QStringList ids;
    if (!m_project) {
        return ids;
    }
    ids.reserve(m_project->images.size());
    for (const TierImage& image : m_project->images) {
        if (!image.id.isEmpty() && !image.assignedTierRowId.has_value()) {
            ids.append(image.id);
        }
    }
    return ids;
}

const TierImage* ImageGalleryPopover::imageForId(const QString& imageId) const {
    return m_project ? m_project->imageById(imageId) : nullptr;
}

QString ImageGalleryPopover::resolvedPathForImage(const TierImage& image) const {
    return m_assetManager && m_project ? m_assetManager->resolvedImagePath(*m_project, image)
                                       : image.sourcePath;
}

QPixmap ImageGalleryPopover::pixmapForImage(const QString& imageId, QSize requestedSize) const {
    if (const TierImage* image = imageForId(imageId)) {
        requestedSize = requestedSize.expandedTo(QSize(m_tileExtent * 2, m_tileExtent * 2));
        if (m_thumbnailCache) {
            if (!m_thumbnailCache->hasThumbnail(imageId, requestedSize)) {
                m_thumbnailCache->requestThumbnail(imageId, resolvedPathForImage(*image),
                                                   requestedSize);
            }
            const QPixmap cached = m_thumbnailCache->thumbnail(imageId, requestedSize);
            if (!cached.isNull()) {
                return cached;
            }
        }
    }
    return vkui::icon(vkui::VkSymbol::Eye, vkui::VkIconRole::Secondary)
        .pixmap(requestedSize.isEmpty() ? QSize(64, 64) : requestedSize);
}

QRect ImageGalleryPopover::cellRect(int index) const {
    if (index < 0 || m_columns <= 0 || m_tileExtent <= 0) {
        return {};
    }
    const int row = index / m_columns;
    const int column = index % m_columns;
    return QRect(column * m_tileExtent, row * m_tileExtent, m_tileExtent, m_tileExtent);
}

int ImageGalleryPopover::cellIndexAt(const QPoint& point) const {
    if (!m_grid || !m_grid->rect().contains(point) || m_tileExtent <= 0 || m_columns <= 0) {
        return -1;
    }
    const int column = point.x() / m_tileExtent;
    const int row = point.y() / m_tileExtent;
    const int index = row * m_columns + column;
    const int count = static_cast<int>(imageIds().size()) + 1;
    return index >= 0 && index < count ? index : -1;
}

void ImageGalleryPopover::recalculateGrid(const QRect& availableGeometry) {
    const int count = qMax(1, static_cast<int>(imageIds().size()) + 1);
    const int maxWidth = qMax(kMinimumTileExtent, qMin(760, availableGeometry.width()));
    const int maxHeight = qMax(kMinimumTileExtent, availableGeometry.height());
    const int maxColumns = qMax(1, qMin(count, maxWidth / kMinimumTileExtent));

    int bestColumns = 1;
    int bestRows = count;
    int bestTile = kMinimumTileExtent;
    qreal bestScore = -std::numeric_limits<qreal>::max();
    for (int columns = 1; columns <= maxColumns; ++columns) {
        const int rows = (count + columns - 1) / columns;
        int tile = qMin(maxWidth / columns, maxHeight / rows);
        tile = qMin(tile, kMaximumTileExtent);
        if (tile < kMinimumTileExtent) {
            continue;
        }
        const int width = columns * tile;
        const int height = rows * tile;
        const qreal aspect = static_cast<qreal>(width) / qMax(1, height);
        const qreal targetAspect = count <= 6 ? 1.2 : 1.55;
        const qreal preferredPenalty = std::abs(tile - kPreferredTileExtent) * 0.55;
        const qreal aspectPenalty = std::abs(aspect - targetAspect) * 24.0;
        const qreal rowPenalty = rows * 1.5;
        const qreal score = tile * 80.0 - preferredPenalty - aspectPenalty - rowPenalty;
        if (score > bestScore) {
            bestScore = score;
            bestColumns = columns;
            bestRows = rows;
            bestTile = tile;
        }
    }

    m_columns = qMax(1, bestColumns);
    m_rows = qMax(1, bestRows);
    m_tileExtent = qBound(kMinimumTileExtent, bestTile, kMaximumTileExtent);
    requestThumbnails();
}

void ImageGalleryPopover::requestThumbnails() {
    if (!m_thumbnailCache || !m_project) {
        return;
    }
    const QSize requestSize(m_tileExtent * 2, m_tileExtent * 2);
    for (const TierImage& image : m_project->images) {
        if (!image.assignedTierRowId.has_value()) {
            m_thumbnailCache->requestThumbnail(image.id, resolvedPathForImage(image), requestSize);
        }
    }
}

} // namespace tlm
