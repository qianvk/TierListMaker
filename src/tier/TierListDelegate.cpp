#include "tier/TierListDelegate.h"

#include "theme/Theme.h"
#include "tier/TierListModel.h"
#include "tier/TierListView.h"

#include <QApplication>
#include <QFontMetrics>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <utility>

#include <vkui/core/VkIcon.h>

namespace tlm {

namespace {
constexpr int kMinimumLabelWidth = 82;
constexpr int kMaximumLabelWidth = 190;
constexpr int kDefaultOuterRadius = 16;
[[maybe_unused]] constexpr int kWindowsOuterRadius = 8;
constexpr int kTileMargin = 0;
constexpr int kTileSpacing = 0;
constexpr int kNominalTileExtent = 84;
constexpr int kMinTileSide = 34;
constexpr int kMaxTileSide = 512;

constexpr qreal kDefaultBackgroundIconVisibility = 0.22;

qreal canvasBackgroundVisibility(const TierProject* project) {
    if (!project) {
        return kDefaultBackgroundIconVisibility;
    }
    const bool hasCustomBackground =
        !project->canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty();
    const qreal fallback = hasCustomBackground ? 1.0 : kDefaultBackgroundIconVisibility;
    return qBound<qreal>(
        0.0,
        project->canvas.value(QStringLiteral("backgroundVisibility"))
            .toDouble(project->canvas.value(QStringLiteral("backgroundImageOpacity"))
                          .toDouble(project->canvas.value(QStringLiteral("backgroundOpacity"))
                                        .toDouble(fallback))),
        1.0);
}

bool imagesVisible(const TierProject* project) {
    return !project || !project->canvas.value(QStringLiteral("previewImagesHidden")).toBool(false);
}

int platformOuterRadius() {
#if defined(Q_OS_WIN)
    return kWindowsOuterRadius;
#else
    return kDefaultOuterRadius;
#endif
}

QPainterPath rowClipPath(const QRect& rect, bool firstRow, bool lastRow) {
    const QRectF rowRect = QRectF(rect).adjusted(0.5, 0.0, -0.5, 0.0);
    if (!firstRow && !lastRow) {
        QPainterPath path;
        path.addRect(rowRect);
        return path;
    }

    QPainterPath rounded;
    const int radius = platformOuterRadius();
    rounded.addRoundedRect(rowRect, radius, radius);

    QPainterPath squareMask;
    if (!firstRow) {
        squareMask.addRect(QRectF(rowRect.left(), rowRect.top(), rowRect.width(), radius + 1));
    }
    if (!lastRow) {
        squareMask.addRect(
            QRectF(rowRect.left(), rowRect.bottom() - radius - 1, rowRect.width(), radius + 1));
    }
    return rounded.united(squareMask);
}

int tileSideForLineHeight(int lineHeight) {
    return qBound(kMinTileSide, lineHeight, kMaxTileSide);
}

int imagesPerLineForTileSide(int viewportWidth, int tileSide, int labelWidth) {
    const int contentWidth = qMax(1, viewportWidth - labelWidth - kTileMargin * 2);
    return qMax(1, (contentWidth + kTileSpacing) / (qMax(kMinTileSide, tileSide) + kTileSpacing));
}

int imagesPerLineForWidth(int viewportWidth) {
    return imagesPerLineForTileSide(viewportWidth, kNominalTileExtent, kMinimumLabelWidth);
}
} // namespace

TierListDelegate::TierListDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void TierListDelegate::setContext(const TierProject* project, const AssetManager* assetManager,
                                  ThumbnailCache* thumbnailCache, QString selectedImageId) {
    m_project = project;
    m_assetManager = assetManager;
    m_thumbnailCache = thumbnailCache;
    m_selectedImageId = std::move(selectedImageId);
}

void TierListDelegate::setSelectedImageId(QString selectedImageId) {
    m_selectedImageId = std::move(selectedImageId);
}

int TierListDelegate::labelWidth() const {
    QFont labelFont = QApplication::font();
    labelFont.setBold(true);
    labelFont.setPointSize(labelFont.pointSize() + 7);
    QFontMetrics metrics(labelFont);
    int width = kMinimumLabelWidth;
    if (m_project) {
        for (const TierRow& row : m_project->rows) {
            width = qMax(width, metrics.horizontalAdvance(row.label) + 28);
        }
    }
    return qBound(kMinimumLabelWidth, width, kMaximumLabelWidth);
}

int TierListDelegate::minimumLabelWidth() {
    return kMinimumLabelWidth;
}

int TierListDelegate::outerRadius() {
    return platformOuterRadius();
}

int TierListDelegate::rowUnitsForImageCount(int imageCount, int viewportWidth) {
    const int perLine = imagesPerLineForWidth(viewportWidth);
    return qMax(1, (qMax(0, imageCount) + perLine - 1) / perLine);
}

int TierListDelegate::rowUnitsForImageCount(int imageCount, int viewportWidth, int lineHeight) {
    return rowUnitsForImageCount(imageCount, viewportWidth, lineHeight, kMinimumLabelWidth);
}

int TierListDelegate::rowUnitsForImageCount(int imageCount, int viewportWidth, int lineHeight,
                                            int labelWidth) {
    const int perLine = imagesPerLineForTileSide(viewportWidth, tileSideForLineHeight(lineHeight),
                                                 qMax(kMinimumLabelWidth, labelWidth));
    return qMax(1, (qMax(0, imageCount) + perLine - 1) / perLine);
}

QRect TierListDelegate::labelRect(const QRect& rowRect) const {
    return QRect(rowRect.left(), rowRect.top(), labelWidth(), rowRect.height());
}

QVector<QRect> TierListDelegate::tileRects(const QModelIndex& index, const QRect& rowRect) const {
    const QStringList imageIds = imageIdsForIndex(index);
    return tileRectsForCount(index, rowRect, static_cast<int>(imageIds.size()));
}

QVector<QRect> TierListDelegate::tileRectsForCount(const QModelIndex& index, const QRect& rowRect,
                                                   int itemCount) const {
    QVector<QRect> rects;
    rects.reserve(qMax(0, itemCount));
    if (itemCount <= 0) {
        return rects;
    }

    const int rowUnits = qMax(1, index.data(TierListModel::RowUnitCountRole).toInt());
    const int lineHeight = qMax(1, qRound(static_cast<qreal>(rowRect.height()) / rowUnits));
    const int currentLabelWidth = labelWidth();
    const QRect contentRect(rowRect.left() + currentLabelWidth + 1, rowRect.top(),
                            qMax(1, rowRect.width() - currentLabelWidth - 2), rowRect.height());
    const int availableWidth = qMax(1, contentRect.width() - kTileMargin * 2);
    const int tileSide = qMax(1, qMin(tileSideForLineHeight(lineHeight), availableWidth));
    const int widthCapacity = qMax(1, (availableWidth + kTileSpacing) / (tileSide + kTileSpacing));
    const int perLine = qMax(1, widthCapacity);

    for (int i = 0; i < itemCount; ++i) {
        const int line = i / perLine;
        const int column = i % perLine;
        const int x = contentRect.left() + kTileMargin + column * (tileSide + kTileSpacing);
        const int lineTop = rowRect.top() + line * lineHeight;
        const int y = lineTop;
        rects.append(QRect(x, y, tileSide, tileSide));
    }
    return rects;
}

QRect TierListDelegate::tileImageRect(const QRect& tileRect) const {
    return tileRect;
}

QString TierListDelegate::imageIdAt(const QModelIndex& index, const QRect& rowRect,
                                    const QPoint& point) const {
    if (!imagesVisible(m_project)) {
        return {};
    }
    const QStringList imageIds = imageIdsForIndex(index);
    const QVector<QRect> rects = tileRects(index, rowRect);
    for (int i = 0; i < rects.size() && i < imageIds.size(); ++i) {
        if (rects.at(i).contains(point)) {
            return imageIds.at(i);
        }
    }
    return {};
}

QRect TierListDelegate::imageRectForId(const QModelIndex& index, const QRect& rowRect,
                                       const QString& imageId) const {
    const QStringList imageIds = imageIdsForIndex(index);
    const QVector<QRect> rects = tileRects(index, rowRect);
    for (int i = 0; i < rects.size() && i < imageIds.size(); ++i) {
        if (imageIds.at(i) == imageId) {
            return tileImageRect(rects.at(i));
        }
    }
    return {};
}

QPixmap TierListDelegate::pixmapForImageId(const QString& imageId) const {
    const TierImage* image = m_project ? m_project->imageById(imageId) : nullptr;
    return image ? pixmapForImage(*image) : QPixmap();
}

QPixmap TierListDelegate::pixmapForImageId(const QString& imageId, QSize targetPixelSize) const {
    const TierImage* image = m_project ? m_project->imageById(imageId) : nullptr;
    return image ? pixmapForImage(*image, targetPixelSize) : QPixmap();
}

QPixmap TierListDelegate::fullPixmapForImageId(const QString& imageId) const {
    const TierImage* image = m_project ? m_project->imageById(imageId) : nullptr;
    if (!image || !m_assetManager || !m_project) {
        return pixmapForImageId(imageId);
    }

    QImageReader reader(m_assetManager->resolvedImagePath(*m_project, *image));
    reader.setAutoTransform(true);
    // Mission Control hover only needs a high-quality display source; capped decoding keeps frames
    // smooth.
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        reader.setScaledSize(sourceSize.scaled(QSize(768, 768), Qt::KeepAspectRatio));
    }
    const QImage fullImage = reader.read();
    return fullImage.isNull() ? pixmapForImage(*image) : QPixmap::fromImage(fullImage);
}

QSize TierListDelegate::sourceSizeForImageId(const QString& imageId) const {
    const TierImage* image = m_project ? m_project->imageById(imageId) : nullptr;
    if (!image || !m_assetManager || !m_project) {
        return {};
    }

    QImageReader reader(m_assetManager->resolvedImagePath(*m_project, *image));
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid()) {
        return size;
    }
    const QPixmap pixmap = pixmapForImage(*image);
    return pixmap.size();
}

int TierListDelegate::insertionIndexForPosition(const QModelIndex& index, const QRect& rowRect,
                                                const QPoint& point) const {
    return insertionIndexForPosition(index, rowRect, point,
                                     static_cast<int>(imageIdsForIndex(index).size()));
}

int TierListDelegate::insertionIndexForPosition(const QModelIndex& index, const QRect& rowRect,
                                                const QPoint& point, int itemCount) const {
    itemCount = qMax(0, itemCount);

    // Project the cursor directly onto the virtual grid. This is more stable than walking
    // painted rects because drag animations and magnification are intentionally paint-only.
    const int rowUnits = qMax(1, index.data(TierListModel::RowUnitCountRole).toInt());
    const int lineHeight = qMax(1, qRound(static_cast<qreal>(rowRect.height()) / rowUnits));
    const int currentLabelWidth = labelWidth();
    const QRect contentRect(rowRect.left() + currentLabelWidth + 1, rowRect.top(),
                            qMax(1, rowRect.width() - currentLabelWidth - 2), rowRect.height());
    const int availableWidth = qMax(1, contentRect.width() - kTileMargin * 2);
    const int tileSide = qMax(1, qMin(tileSideForLineHeight(lineHeight), availableWidth));
    const int step = qMax(1, tileSide + kTileSpacing);
    const int perLine = qMax(1, (availableWidth + kTileSpacing) / step);
    const int totalSlots = itemCount + 1;
    const int maxLine = qMax(0, (totalSlots - 1) / perLine);

    const int line = qBound(0, (point.y() - rowRect.top()) / lineHeight, maxLine);
    const int relativeX = point.x() - contentRect.left() - kTileMargin;
    int column = 0;
    if (relativeX > 0) {
        column = relativeX / step;
        const int cellX = relativeX - column * step;
        if (cellX > tileSide / 2) {
            ++column;
        }
    }
    column = qBound(0, column, perLine);
    return qBound(0, line * perLine + column, itemCount);
}

void TierListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    if (!index.isValid()) {
        return;
    }

    QStyleOptionViewItem paintOption(option);
    const TierListView* view = qobject_cast<const TierListView*>(option.widget);
    if (!view && option.widget) {
        view = qobject_cast<const TierListView*>(option.widget->parentWidget());
    }
    if (view) {
        if (view->isReorderSourceIndex(index)) {
            return;
        }
        const qreal offset = view->visualOffsetForIndex(index);
        if (!qFuzzyIsNull(offset)) {
            paintOption.rect.translate(0, qRound(offset));
        }
    }
    // Row and gallery Mission Control share the same board-material transition,
    // including the leading-label slide. The image layer decides its own source.
    const qreal missionProgress = view ? view->missionTransitionProgress() : 0.0;

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);

    const QRect rowRect = paintOption.rect;
    const bool firstRow =
        view ? view->isVisualFirstRow(index) : index.data(TierListModel::FirstRowRole).toBool();
    const bool lastRow =
        view ? view->isVisualLastRow(index) : index.data(TierListModel::LastRowRole).toBool();
    const QColor rowColor = index.data(TierListModel::ColorRole).value<QColor>();
    const ThemeTokens& colors = activeThemeTokens();
    const qreal backgroundVisibility = canvasBackgroundVisibility(m_project);
    // When a background image is present, rows act like a terminal-style material layer:
    // visibility 0 restores the solid board; visibility 1 leaves the image unobstructed.
    const float labelAlpha = 1.0F;
    const float materialAlpha = static_cast<float>(1.0 - backgroundVisibility);
    const float gridAlpha = static_cast<float>(1.0 - backgroundVisibility * 0.72);

    painter->setClipPath(rowClipPath(rowRect, firstRow, lastRow));
    QColor labelColor = rowColor.isValid() ? rowColor : colors.controlFillHovered;
    labelColor.setAlphaF(labelAlpha);
    const int currentLabelWidth = labelWidth();
    const int labelSlide = qRound((currentLabelWidth + 2) * missionProgress);
    const QRect labelPaintRect = labelRect(rowRect).translated(-labelSlide, 0);
    painter->fillRect(labelPaintRect, labelColor);
    QColor contentBackground = colors.tierRowBackground;
    contentBackground.setAlphaF(materialAlpha);
    const int animatedSplitX = rowRect.left() + qRound(currentLabelWidth * (1.0 - missionProgress));
    painter->fillRect(QRect(animatedSplitX, rowRect.top(),
                            qMax(0, rowRect.right() - animatedSplitX + 1), rowRect.height()),
                      contentBackground);

    QFont labelFont = option.font;
    labelFont.setBold(true);
    labelFont.setPointSize(labelFont.pointSize() + 7);
    painter->setFont(labelFont);
    QColor textColor = contrastingTextColor(labelColor);
    textColor.setAlphaF(1.0F);
    painter->setPen(textColor);
    painter->drawText(
        labelPaintRect.adjusted(5, 5, -5, -5), Qt::AlignCenter,
        painter->fontMetrics().elidedText(index.data(TierListModel::LabelRole).toString(),
                                          Qt::ElideRight, currentLabelWidth - 10));

    painter->setClipping(false);
    painter->setRenderHint(QPainter::Antialiasing, false);
    QColor gridLine = colors.separator;
    gridLine.setAlphaF(gridAlpha * static_cast<float>(1.0 - missionProgress));
    painter->setPen(QPen(gridLine, 1));
    painter->drawLine(animatedSplitX, rowRect.top(), animatedSplitX, rowRect.bottom());
    if (!lastRow) {
        painter->drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());
    }
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (missionProgress > 0.001) {
        painter->restore();
        return;
    }

    const QStringList imageIds = imagesVisible(m_project) ? imageIdsForIndex(index) : QStringList();
    const QVector<QRect> rects = tileRects(index, rowRect);
    struct PaintTile {
        const TierImage* image{nullptr};
        QRect baseRect;
        QRectF paintRect;
        qreal scale{1.0};
        bool selected{false};
    };
    QVector<PaintTile> paintTiles;
    paintTiles.reserve(qMin(rects.size(), imageIds.size()));

    for (int i = 0; i < rects.size() && i < imageIds.size(); ++i) {
        const TierImage* image = m_project ? m_project->imageById(imageIds.at(i)) : nullptr;
        if (!image) {
            continue;
        }
        if (view && view->isImageDragSource(image->id)) {
            continue;
        }

        QRect tileRect = rects.at(i);
        if (view) {
            const QPointF offset = view->visualOffsetForImage(image->id);
            if (!offset.isNull()) {
                tileRect.translate(qRound(offset.x()), qRound(offset.y()));
            }
        }
        const qreal scale = view ? view->dockScaleForImage(index, tileRect, image->id) : 1.0;
        const QPointF dockOffset =
            view ? view->dockOffsetForImage(index, tileRect, image->id) : QPointF();
        const QSizeF paintSize(tileRect.width() * scale, tileRect.height() * scale);
        const QPointF center = QPointF(tileRect.center()) + dockOffset;
        const QRectF paintRect(center.x() - paintSize.width() / 2.0,
                               center.y() - paintSize.height() / 2.0, paintSize.width(),
                               paintSize.height());
        paintTiles.append(
            PaintTile{image, tileRect, paintRect, scale, image->id == m_selectedImageId});
    }

    std::sort(paintTiles.begin(), paintTiles.end(),
              [](const PaintTile& left, const PaintTile& right) {
                  if (!qFuzzyCompare(left.scale, right.scale)) {
                      return left.scale < right.scale;
                  }
                  return left.baseRect.left() < right.baseRect.left();
              });

    for (const PaintTile& tile : std::as_const(paintTiles)) {
        const TierImage* image = tile.image;
        const QRectF imageRect = tile.paintRect;
        const bool selected = tile.selected;

        const qreal deviceRatio =
            option.widget ? option.widget->devicePixelRatioF() : qApp->devicePixelRatio();
        const QSize targetPixelSize(qCeil(imageRect.width() * deviceRatio),
                                    qCeil(imageRect.height() * deviceRatio));
        QPixmap pixmap = pixmapForImage(*image, targetPixelSize);
        painter->save();
        const QRectF drawBounds = imageRect;
        painter->setClipRect(drawBounds);
        painter->fillRect(drawBounds, colors.elevatedBackground);
        if (!pixmap.isNull()) {
            painter->drawPixmap(
                drawBounds, pixmap,
                image->thumbnailSourceRect(pixmap.size(), drawBounds.size().toSize()));
        }
        painter->restore();

        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(selected ? colors.accent : colors.imageBorder, selected ? 2 : 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(imageRect.adjusted(0.5, 0.5, -0.5, -0.5));
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    painter->restore();
}

QSize TierListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    Q_UNUSED(option);
    return QSize(1, qMax(1, index.data(TierListModel::RowHeightRole).toInt()));
}

QStringList TierListDelegate::imageIdsForIndex(const QModelIndex& index) const {
    return index.data(TierListModel::ImageIdsRole).toStringList();
}

QPixmap TierListDelegate::pixmapForImage(const TierImage& image, QSize targetPixelSize) const {
    if (m_thumbnailCache) {
        if (m_project && m_assetManager) {
            if (!m_thumbnailCache->hasThumbnail(image.id, targetPixelSize)) {
                m_thumbnailCache->requestThumbnail(
                    image.id, m_assetManager->resolvedImagePath(*m_project, image),
                    targetPixelSize.isEmpty() ? QSize(192, 192) : targetPixelSize);
            }
        }
        const QPixmap cached = m_thumbnailCache->thumbnail(image.id, targetPixelSize);
        if (!cached.isNull()) {
            return cached;
        }
    }
    return vkui::icon(vkui::VkSymbol::Eye, vkui::VkIconRole::Secondary)
        .pixmap(targetPixelSize.isEmpty() ? QSize(64, 64) : targetPixelSize);
}

} // namespace tlm
