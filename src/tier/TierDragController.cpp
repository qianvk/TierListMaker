#include "tier/TierDragController.h"

#include "logging/Logger.h"

#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

namespace tlm {

namespace {
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

QPixmap centerCroppedDragPixmap(const QPixmap& pixmap, const QSize& logicalSize, qreal devicePixelRatio) {
    if (pixmap.isNull() || logicalSize.isEmpty()) {
        return {};
    }

    QPixmap result(logicalSize * devicePixelRatio);
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRect target(QPoint(0, 0), logicalSize);
    painter.drawPixmap(target, pixmap, centeredCropSourceRect(pixmap, target.size()));
    return result;
}
} // namespace

QString TierDragController::imageMimeType() {
    return QStringLiteral("application/x-tierlistmaker-image-id");
}

QString TierDragController::rowMimeType() {
    return QStringLiteral("application/x-tierlistmaker-row-id");
}

QMimeData* TierDragController::createMimeData(const QString& imageId) {
    auto* mimeData = new QMimeData;
    mimeData->setData(imageMimeType(), imageId.toUtf8());
    return mimeData;
}

QMimeData* TierDragController::createRowMimeData(const QString& rowId) {
    auto* mimeData = new QMimeData;
    mimeData->setData(rowMimeType(), rowId.toUtf8());
    return mimeData;
}

QString TierDragController::imageIdFromMimeData(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasFormat(imageMimeType())) {
        return {};
    }
    return QString::fromUtf8(mimeData->data(imageMimeType()));
}

QString TierDragController::rowIdFromMimeData(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasFormat(rowMimeType())) {
        return {};
    }
    return QString::fromUtf8(mimeData->data(rowMimeType()));
}

ImageTileWidget::ImageTileWidget(const TierProject* project, const TierImage* image,
                                 const AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                                 QWidget* parent)
    : QFrame(parent),
      m_project(project),
      m_image(image),
      m_assetManager(assetManager),
      m_thumbnailCache(thumbnailCache),
      m_imageId(image ? image->id : QString()) {
    setTileExtent(m_tileExtent);
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    if (m_thumbnailCache && image) {
        connect(m_thumbnailCache, &ThumbnailCache::thumbnailReady, this, [this](const QString& key) {
            if (key == m_imageId) {
                update();
            }
        });
        m_thumbnailCache->requestThumbnail(m_imageId, resolvedPath(), QSize(144, 144));
    }
}

void ImageTileWidget::setSelected(bool selected) {
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

void ImageTileWidget::setTileExtent(int extent) {
    m_tileExtent = qBound(48, extent, 128);
    setFixedSize(m_tileExtent, m_tileExtent);
    if (m_thumbnailCache && !m_imageId.isEmpty()) {
        m_thumbnailCache->requestThumbnail(m_imageId, resolvedPath(),
                                           QSize(m_tileExtent * 2, m_tileExtent * 2));
    }
    updateGeometry();
    update();
}

QRect ImageTileWidget::imageRectIn(const QWidget* ancestor) const {
    const QRect local = rect();
    if (!ancestor) {
        return local;
    }
    return QRect(mapTo(const_cast<QWidget*>(ancestor), local.topLeft()), local.size());
}

void ImageTileWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPosition = event->pos();
        m_dragInProgress = false;
        setFocus(Qt::MouseFocusReason);
        Logger::debug(QStringLiteral("tier.image.tile.press imageId=%1 pos=(%2,%3)")
                          .arg(m_imageId)
                          .arg(event->pos().x())
                          .arg(event->pos().y()));
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void ImageTileWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    if (m_dragInProgress) {
        return;
    }
    m_dragInProgress = true;
    emit dragActiveChanged(true);
    const QString imageId = m_imageId;
    auto* drag = new QDrag(window() ? window() : this);
    drag->setMimeData(TierDragController::createMimeData(imageId));
    QPixmap dragPixmap = pixmap();
    if (!dragPixmap.isNull()) {
        drag->setPixmap(centerCroppedDragPixmap(dragPixmap, rect().size(), devicePixelRatioF()));
        drag->setHotSpot(event->pos());
    }
    setCursor(Qt::ClosedHandCursor);
    QPointer<ImageTileWidget> guard(this);
    Logger::info(QStringLiteral("tier.image.tile.drag.start imageId=%1").arg(imageId));
    const Qt::DropAction result = drag->exec(Qt::MoveAction);
    if (!guard) {
        Logger::info(QStringLiteral("tier.image.tile.drag.finish imageId=%1 result=%2 widgetAlive=false")
                         .arg(imageId)
                         .arg(static_cast<int>(result)));
        return;
    }
    Logger::info(QStringLiteral("tier.image.tile.drag.finish imageId=%1 result=%2")
                     .arg(imageId)
                     .arg(static_cast<int>(result)));
    m_dragInProgress = false;
    emit dragActiveChanged(false);
    setCursor(Qt::OpenHandCursor);
    event->accept();
}

void ImageTileWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (!m_dragInProgress) {
            emit selected(m_imageId);
            Logger::debug(QStringLiteral("tier.image.tile.select imageId=%1").arg(m_imageId));
        }
        m_dragInProgress = false;
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void ImageTileWidget::mouseDoubleClickEvent(QMouseEvent*) {
    Logger::info(QStringLiteral("tier.image.tile.preview.request source=double-click imageId=%1").arg(m_imageId));
    emit previewRequested(m_imageId, imageRectIn(window()));
}

void ImageTileWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        Logger::info(QStringLiteral("tier.image.tile.preview.request source=space imageId=%1").arg(m_imageId));
        emit previewRequested(m_imageId, imageRectIn(window()));
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ImageTileWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRect tileRect = rect();
    const QRect imageRect = tileRect;

    painter.fillRect(tileRect, palette().color(QPalette::AlternateBase));
    painter.save();
    painter.setClipRect(imageRect);
    QPixmap p = pixmap();
    if (!p.isNull()) {
        painter.drawPixmap(imageRect, p, centeredCropSourceRect(p, imageRect.size()));
    }
    painter.restore();

    const QString text = m_image ? m_image->displayName : QString();
    if (!text.isEmpty()) {
        const QRect bannerRect = imageRect.adjusted(0, imageRect.height() - qMax(18, imageRect.height() / 4), 0, 0);
        QLinearGradient gradient(bannerRect.topLeft(), bannerRect.bottomLeft());
        gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
        gradient.setColorAt(1.0, QColor(0, 0, 0, 150));
        painter.fillRect(bannerRect, gradient);
        painter.setPen(Qt::white);
        QFont textFont = painter.font();
        textFont.setPixelSize(qMax(10, imageRect.height() / 7));
        painter.setFont(textFont);
        painter.drawText(bannerRect.adjusted(6, 0, -6, -2), Qt::AlignLeft | Qt::AlignBottom,
                         painter.fontMetrics().elidedText(text, Qt::ElideRight, bannerRect.width() - 12));
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(m_selected || hasFocus() ? palette().highlight().color() : QColor(0, 0, 0, 42),
                        m_selected || hasFocus() ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(tileRect.adjusted(0, 0, -1, -1));
}

QSize ImageTileWidget::sizeHint() const {
    return QSize(m_tileExtent, m_tileExtent);
}

QString ImageTileWidget::resolvedPath() const {
    if (!m_project || !m_image) {
        return {};
    }
    return m_assetManager ? m_assetManager->resolvedImagePath(*m_project, *m_image) : m_image->sourcePath;
}

QPixmap ImageTileWidget::pixmap() const {
    if (m_thumbnailCache && m_thumbnailCache->hasThumbnail(m_imageId)) {
        return m_thumbnailCache->thumbnail(m_imageId);
    }
    return QPixmap(QStringLiteral(":/icons/image.svg"));
}

} // namespace tlm
