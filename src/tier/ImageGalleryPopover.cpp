#include "tier/ImageGalleryPopover.h"

#include "logging/Logger.h"
#include "tier/TierDragController.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QShowEvent>
#include <QHideEvent>
#include <QMenu>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tlm {

namespace {
constexpr int kPopoverArrowHeight = 12;
constexpr int kPopoverArrowCenterX = 46;
constexpr int kMinimumTileExtent = 44;
constexpr int kPreferredTileExtent = 72;
constexpr int kMaximumTileExtent = 86;

int platformPopoverRadius() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return 16;
#else
    return 10;
#endif
}

QColor popoverSurfaceColor(const QPalette& palette) {
    return palette.color(QPalette::Base).lightness() < 96 ? QColor(31, 35, 53)
                                                          : QColor(250, 251, 253);
}

QColor popoverStrokeColor(const QPalette& palette) {
    return palette.color(QPalette::Base).lightness() < 96 ? QColor(68, 76, 110)
                                                          : QColor(210, 218, 232);
}

QPainterPath roundedPopoverPath(const QRectF& bubbleRect, qreal arrowCenterX, qreal arrowHeight,
                                qreal radius) {
    QPainterPath bubble;
    bubble.addRoundedRect(bubbleRect, radius, radius);

    if (bubbleRect.width() <= radius * 2.0 || arrowHeight <= 0.0) {
        return bubble;
    }

    constexpr qreal kArrowHalfWidth = 18.0;
    constexpr qreal kTipHalfWidth = 4.2;
    constexpr qreal kBodyOverlap = 2.5;
    const qreal minCenter = bubbleRect.left() + radius + kArrowHalfWidth;
    const qreal maxCenter = bubbleRect.right() - radius - kArrowHalfWidth;
    const qreal centerX = qBound(minCenter, arrowCenterX, maxCenter);
    const qreal baseY = bubbleRect.top() + kBodyOverlap;
    const qreal tipY = bubbleRect.top() - arrowHeight + 0.5;

    // The arrow overlaps the body, then both paths are unioned so the shared edge is never stroked.
    QPainterPath arrow;
    arrow.moveTo(centerX - kArrowHalfWidth, baseY);
    arrow.cubicTo(centerX - 13.0, baseY, centerX - 9.5, tipY + 6.0,
                  centerX - kTipHalfWidth, tipY + 2.8);
    arrow.quadTo(centerX, tipY - 0.4, centerX + kTipHalfWidth, tipY + 2.8);
    arrow.cubicTo(centerX + 9.5, tipY + 6.0, centerX + 13.0, baseY,
                  centerX + kArrowHalfWidth, baseY);
    arrow.closeSubpath();

    return bubble.united(arrow);
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
                       sourceRect.isValid() ? sourceRect : centeredCropSourceRect(pixmap, logicalSize));
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
        return QRect(mapTo(window(), localRect.topLeft()), localRect.size());
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_owner) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            platformPopoverRadius(), platformPopoverRadius());
        painter.setClipPath(clip);

        painter.fillPath(clip, popoverSurfaceColor(palette()));

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
        if (m_dragging || (event->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
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
            const QRect crop = image ? image->thumbnailSourceRect(pixmap.size(), sourceRect.size()) : QRect();
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
            Logger::info(QStringLiteral("tier.gallery.preview.request source=double-click imageId=%1")
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
        menu.setAttribute(Qt::WA_TranslucentBackground, false);
        QAction* editAction = menu.addAction(tr("Edit"));
        QAction* removeAction = menu.addAction(tr("Remove from Image Gallery"));
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
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(cell, palette().color(QPalette::AlternateBase));
        const QPixmap pixmap = m_owner->pixmapForImage(imageId, cell.size() * 2);
        if (!pixmap.isNull()) {
            const TierImage* image = m_owner->imageForId(imageId);
            painter->drawPixmap(cell, pixmap,
                                image ? image->thumbnailSourceRect(pixmap.size(), cell.size())
                                      : centeredCropSourceRect(pixmap, cell.size()));
        }
        if (imageId == m_owner->m_selectedImageId) {
            painter->setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cell.adjusted(1, 1, -2, -2));
        }
        painter->restore();
    }

    void paintImportCell(QPainter* painter, const QRect& cell) {
        painter->save();
        const bool dark = palette().color(QPalette::Base).lightness() < 96;
        painter->fillRect(cell, dark ? QColor(44, 50, 73) : QColor(237, 241, 247));
        const QColor stroke = dark ? QColor(167, 178, 210) : QColor(73, 86, 110);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(stroke, qMax(2, cell.width() / 28), Qt::SolidLine, Qt::RoundCap));
        const int arm = qMax(11, cell.width() / 5);
        const QPoint center = cell.center();
        painter->drawLine(QPoint(center.x() - arm, center.y()), QPoint(center.x() + arm, center.y()));
        painter->drawLine(QPoint(center.x(), center.y() - arm), QPoint(center.x(), center.y() + arm));
        painter->restore();
    }

    ImageGalleryPopover* m_owner{nullptr};
    QPoint m_pressPosition;
    int m_pressedIndex{-1};
    bool m_dragging{false};
};

ImageGalleryPopover::ImageGalleryPopover(QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_grid(new GalleryGridWidget(this)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setObjectName(QStringLiteral("ImageGalleryPopover"));
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
        m_thumbnailConnection = connect(m_thumbnailCache, &ThumbnailCache::thumbnailReady,
                                        m_grid, [this](const QString&) { m_grid->update(); });
    }
    requestThumbnails();
    if (isVisible()) {
        QScreen* screen = QGuiApplication::screenAt(geometry().center());
        const QRect available = screen ? screen->availableGeometry().adjusted(10, 10, -10, -10)
                                       : QRect(QPoint(10, 10), QSize(820, 620));
        recalculateGrid(available);
        resize(sizeHint());
        if (m_grid) {
            m_grid->setGeometry(0, kPopoverArrowHeight, width(), height() - kPopoverArrowHeight);
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
    Logger::debug(QStringLiteral("tier.gallery.popover.dismiss.suspended value=%1").arg(suspended));
}

void ImageGalleryPopover::placeBelow(const QRect& globalAnchorRect) {
    m_anchorGlobalRect = globalAnchorRect;
    QScreen* screen = globalAnchorRect.isValid() ? QGuiApplication::screenAt(globalAnchorRect.center())
                                                 : QGuiApplication::screenAt(QCursor::pos());
    const QRect available = screen ? screen->availableGeometry().adjusted(10, 10, -10, -10)
                                   : QRect(QPoint(10, 10), QSize(820, 620));
    recalculateGrid(available);
    const QSize popupSize = sizeHint();
    resize(popupSize);

    const int anchorCenterX = globalAnchorRect.isValid()
                                  ? globalAnchorRect.center().x()
                                  : available.center().x();
    QPoint topLeft = globalAnchorRect.isValid()
                         ? QPoint(globalAnchorRect.center().x() - kPopoverArrowCenterX,
                                  globalAnchorRect.bottom() + 8)
                         : QPoint(available.center().x() - popupSize.width() / 2, available.top() + 54);
    const int maxX = qMax(available.left(), available.right() - popupSize.width());
    const int maxY = qMax(available.top(), available.bottom() - popupSize.height());
    topLeft.setX(qBound(available.left(), topLeft.x(), maxX));
    topLeft.setY(qBound(available.top(), topLeft.y(), maxY));

    const int radius = platformPopoverRadius();
    m_arrowCenterX = qBound(radius + 14, anchorCenterX - topLeft.x(), popupSize.width() - radius - 14);
    move(topLeft);
    if (m_grid) {
        m_grid->setGeometry(0, kPopoverArrowHeight, width(), height() - kPopoverArrowHeight);
    }
    update();
    Logger::debug(QStringLiteral("tier.gallery.popover.place images=%1 columns=%2 rows=%3 tile=%4 rect=(%5,%6,%7,%8)")
                      .arg(imageIds().size())
                      .arg(m_columns)
                      .arg(m_rows)
                      .arg(m_tileExtent)
                      .arg(x())
                      .arg(y())
                      .arg(width())
                      .arg(height()));
}

QRect ImageGalleryPopover::imageSourceRect(const QString& imageId) const {
    return m_grid ? m_grid->imageSourceRect(imageId) : QRect();
}

QSize ImageGalleryPopover::sizeHint() const {
    return QSize(qMax(1, m_columns) * m_tileExtent,
                 qMax(1, m_rows) * m_tileExtent + kPopoverArrowHeight);
}

bool ImageGalleryPopover::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    if (!isVisible() || m_outsideDismissSuspended) {
        return false;
    }

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    if (event->type() == QEvent::WindowDeactivate) {
        Logger::debug(QStringLiteral("tier.gallery.popover.close reason=window-deactivate"));
        close();
        return false;
    }
#endif

    if (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonDblClick &&
        event->type() != QEvent::NonClientAreaMouseButtonPress) {
        return false;
    }

    const auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (!shouldDismissForGlobalPosition(mouseEvent->globalPosition().toPoint())) {
        return false;
    }

    Logger::debug(QStringLiteral("tier.gallery.popover.close reason=outside-click"));
    close();
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    return true;
#else
    return false;
#endif
}

void ImageGalleryPopover::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (!m_eventFilterInstalled && qApp) {
        qApp->installEventFilter(this);
        m_eventFilterInstalled = true;
    }
}

void ImageGalleryPopover::hideEvent(QHideEvent* event) {
    if (m_eventFilterInstalled && qApp) {
        qApp->removeEventFilter(this);
        m_eventFilterInstalled = false;
    }
    QDialog::hideEvent(event);
}

void ImageGalleryPopover::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bubbleRect = QRectF(rect()).adjusted(0.5, kPopoverArrowHeight + 0.5, -0.5, -0.5);
    const int radius = platformPopoverRadius();
    const QPainterPath path =
        roundedPopoverPath(bubbleRect, m_arrowCenterX, kPopoverArrowHeight, radius);

    QColor fill = popoverSurfaceColor(palette());
    fill.setAlpha(255);
    QColor stroke = popoverStrokeColor(palette());
    stroke.setAlpha(palette().color(QPalette::Base).lightness() < 96 ? 210 : 150);
    painter.fillPath(path, fill);
    painter.setPen(QPen(stroke, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void ImageGalleryPopover::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (m_grid) {
        m_grid->setGeometry(0, kPopoverArrowHeight, width(), height() - kPopoverArrowHeight);
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
    return m_assetManager && m_project ? m_assetManager->resolvedImagePath(*m_project, image) : image.sourcePath;
}

QPixmap ImageGalleryPopover::pixmapForImage(const QString& imageId, QSize requestedSize) const {
    if (const TierImage* image = imageForId(imageId)) {
        requestedSize = requestedSize.expandedTo(QSize(m_tileExtent * 2, m_tileExtent * 2));
        if (m_thumbnailCache) {
            if (!m_thumbnailCache->hasThumbnail(imageId, requestedSize)) {
                m_thumbnailCache->requestThumbnail(imageId, resolvedPathForImage(*image), requestedSize);
            }
            const QPixmap cached = m_thumbnailCache->thumbnail(imageId, requestedSize);
            if (!cached.isNull()) {
                return cached;
            }
        }
    }
    return QPixmap(QStringLiteral(":/icons/image.svg"));
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
    const int maxHeight = qMax(kMinimumTileExtent, availableGeometry.height() - kPopoverArrowHeight);
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

bool ImageGalleryPopover::shouldDismissForGlobalPosition(const QPoint& globalPosition) const {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (m_anchorGlobalRect.isValid() && m_anchorGlobalRect.adjusted(-3, -3, 3, 3).contains(globalPosition)) {
        // Let the gallery toolbar button deliver its clicked() signal; EditPage will close the popover.
        return false;
    }
#else
    Q_UNUSED(m_anchorGlobalRect);
#endif
    return !geometry().contains(globalPosition);
}

} // namespace tlm
