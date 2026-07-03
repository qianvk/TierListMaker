#include "tier/TierListDelegate.h"

#include "tier/TierListView.h"
#include "tier/TierListModel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <utility>

namespace tlm {

namespace {
constexpr int kMinimumLabelWidth = 82;
constexpr int kMaximumLabelWidth = 190;
constexpr int kOuterRadius = 16;
constexpr int kTileMargin = 0;
constexpr int kTileSpacing = 0;
constexpr int kNominalTileExtent = 84;
constexpr int kMinTileSide = 34;
constexpr int kMaxTileSide = 512;

const QColor kContentBackground(QStringLiteral("#f7f8fb"));
const QColor kGridLine(QStringLiteral("#c8d0da"));
const QColor kTextColor(QStringLiteral("#111111"));
const QColor kTileBackground(QStringLiteral("#ffffff"));

bool hasCanvasBackground(const TierProject* project) {
    return project && !project->canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty();
}

qreal canvasBackgroundVisibility(const TierProject* project) {
    if (!hasCanvasBackground(project)) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        project->canvas.value(QStringLiteral("backgroundVisibility"))
            .toDouble(project->canvas.value(QStringLiteral("backgroundImageOpacity")).toDouble(
                project->canvas.value(QStringLiteral("backgroundOpacity")).toDouble(1.0))),
        1.0);
}

bool imagesVisible(const TierProject* project) {
    return !project || !project->canvas.value(QStringLiteral("previewImagesHidden")).toBool(false);
}

QPainterPath rowClipPath(const QRect& rect, bool firstRow, bool lastRow) {
    const QRectF rowRect = QRectF(rect).adjusted(0.5, 0.0, -0.5, 0.0);
    if (!firstRow && !lastRow) {
        QPainterPath path;
        path.addRect(rowRect);
        return path;
    }

    QPainterPath rounded;
    rounded.addRoundedRect(rowRect, kOuterRadius, kOuterRadius);

    QPainterPath squareMask;
    if (!firstRow) {
        squareMask.addRect(QRectF(rowRect.left(), rowRect.top(), rowRect.width(), kOuterRadius + 1));
    }
    if (!lastRow) {
        squareMask.addRect(QRectF(rowRect.left(), rowRect.bottom() - kOuterRadius - 1,
                                  rowRect.width(), kOuterRadius + 1));
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
} // namespace

TierListDelegate::TierListDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void TierListDelegate::setContext(const TierProject* project, const AssetManager* assetManager,
                                  ThumbnailCache* thumbnailCache, QString selectedImageId) {
    m_project = project;
    m_assetManager = assetManager;
    m_thumbnailCache = thumbnailCache;
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
    return kOuterRadius;
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

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                            QPainter::SmoothPixmapTransform);

    const QRect rowRect = paintOption.rect;
    const bool firstRow = view ? view->isVisualFirstRow(index)
                               : index.data(TierListModel::FirstRowRole).toBool();
    const bool lastRow = view ? view->isVisualLastRow(index)
                              : index.data(TierListModel::LastRowRole).toBool();
    const QColor rowColor = index.data(TierListModel::ColorRole).value<QColor>();
    const qreal backgroundVisibility = canvasBackgroundVisibility(m_project);
    // When a background image is present, rows act like a terminal-style material layer:
    // visibility 0 restores the solid board; visibility 1 leaves the image unobstructed.
    const float labelAlpha = 1.0F;
    const float materialAlpha = static_cast<float>(1.0 - backgroundVisibility);
    const float gridAlpha = static_cast<float>(1.0 - backgroundVisibility * 0.72);

    painter->setClipPath(rowClipPath(rowRect, firstRow, lastRow));
    QColor labelColor = rowColor.isValid() ? rowColor : QColor(QStringLiteral("#d9dee7"));
    labelColor.setAlphaF(labelAlpha);
    painter->fillRect(labelRect(rowRect), labelColor);
    QColor contentBackground = option.palette.color(QPalette::AlternateBase);
    if (!contentBackground.isValid()) {
        contentBackground = kContentBackground;
    }
    contentBackground.setAlphaF(materialAlpha);
    const int currentLabelWidth = labelWidth();
    painter->fillRect(QRect(rowRect.left() + currentLabelWidth, rowRect.top(),
                            qMax(0, rowRect.width() - currentLabelWidth), rowRect.height()),
                      contentBackground);

    QFont labelFont = option.font;
    labelFont.setBold(true);
    labelFont.setPointSize(labelFont.pointSize() + 7);
    painter->setFont(labelFont);
    QColor textColor = kTextColor;
    textColor.setAlphaF(1.0F);
    painter->setPen(textColor);
    painter->drawText(labelRect(rowRect).adjusted(5, 5, -5, -5), Qt::AlignCenter,
                      painter->fontMetrics().elidedText(index.data(TierListModel::LabelRole).toString(),
                                                        Qt::ElideRight, currentLabelWidth - 10));

    painter->setClipping(false);
    painter->setRenderHint(QPainter::Antialiasing, false);
    QColor gridLine = kGridLine;
    gridLine.setAlphaF(gridAlpha);
    painter->setPen(QPen(gridLine, 1));
    painter->drawLine(rowRect.left() + currentLabelWidth, rowRect.top(),
                      rowRect.left() + currentLabelWidth, rowRect.bottom());
    if (!lastRow) {
        painter->drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());
    }
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QStringList imageIds = imagesVisible(m_project) ? imageIdsForIndex(index) : QStringList();
    const QVector<QRect> rects = tileRects(index, rowRect);
    QFont imageFont = option.font;
    imageFont.setPointSize(qMax(8, imageFont.pointSize() - 2));

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
        const QPointF dockOffset = view ? view->dockOffsetForImage(index, tileRect, image->id) : QPointF();
        const QSizeF paintSize(tileRect.width() * scale, tileRect.height() * scale);
        const QPointF center = QPointF(tileRect.center()) + dockOffset;
        const QRectF paintRect(center.x() - paintSize.width() / 2.0,
                               center.y() - paintSize.height() / 2.0,
                               paintSize.width(), paintSize.height());
        paintTiles.append(PaintTile{image, tileRect, paintRect, scale, image->id == m_selectedImageId});
    }

    std::sort(paintTiles.begin(), paintTiles.end(), [](const PaintTile& left, const PaintTile& right) {
        if (!qFuzzyCompare(left.scale, right.scale)) {
            return left.scale < right.scale;
        }
        return left.baseRect.left() < right.baseRect.left();
    });

    for (const PaintTile& tile : std::as_const(paintTiles)) {
        const TierImage* image = tile.image;
        const QRectF imageRect = tile.paintRect;
        const bool selected = tile.selected;

        QPixmap pixmap = pixmapForImage(*image);
        painter->save();
        const QRectF drawBounds = imageRect;
        painter->setClipRect(drawBounds);
        painter->fillRect(drawBounds, option.palette.alternateBase());
        if (!pixmap.isNull()) {
            painter->drawPixmap(drawBounds, pixmap, centeredCropSourceRect(pixmap, drawBounds.size().toSize()));
        }
        painter->restore();

        if (imageRect.height() > 34 && !image->displayName.isEmpty()) {
            const QRectF bannerRect =
                imageRect.adjusted(0, imageRect.height() - qMax<qreal>(18.0, imageRect.height() / 4.0),
                                   0, 0);
            QLinearGradient gradient(bannerRect.topLeft(), bannerRect.bottomLeft());
            gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
            gradient.setColorAt(1.0, QColor(0, 0, 0, 150));
            painter->fillRect(bannerRect, gradient);
            painter->setFont(imageFont);
            painter->setPen(Qt::white);
            painter->drawText(bannerRect.adjusted(5, 0, -5, -2), Qt::AlignLeft | Qt::AlignBottom,
                              painter->fontMetrics().elidedText(image->displayName, Qt::ElideRight,
                                                                qRound(bannerRect.width()) - 10));
        }

        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(selected ? option.palette.highlight().color() : QColor(0, 0, 0, 42),
                             selected ? 2 : 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(imageRect.adjusted(0.5, 0.5, -0.5, -0.5));
        painter->setRenderHint(QPainter::Antialiasing, true);
    }

    painter->restore();
}

QSize TierListDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    return QSize(1, qMax(1, index.data(TierListModel::RowHeightRole).toInt()));
}

QStringList TierListDelegate::imageIdsForIndex(const QModelIndex& index) const {
    return index.data(TierListModel::ImageIdsRole).toStringList();
}

QPixmap TierListDelegate::pixmapForImage(const TierImage& image) const {
    if (m_thumbnailCache) {
        if (m_thumbnailCache->hasThumbnail(image.id)) {
            return m_thumbnailCache->thumbnail(image.id);
        }
        if (m_project && m_assetManager) {
            m_thumbnailCache->requestThumbnail(image.id, m_assetManager->resolvedImagePath(*m_project, image),
                                               QSize(160, 160));
        }
    }
    return QPixmap(QStringLiteral(":/icons/image.svg"));
}

} // namespace tlm
