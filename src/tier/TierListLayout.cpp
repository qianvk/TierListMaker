#include "tier/TierListLayout.h"

#include <algorithm>
#include <utility>

namespace tlm {

namespace {
constexpr int kContentLeadingBorder = 1;
constexpr int kContentTrailingBorder = 1;
constexpr int kTileMargin = 0;
constexpr int kTileSpacing = 0;
constexpr int kMaximumTileSide = 512;

int tileSideForLineHeight(int lineHeight) {
    return std::clamp(lineHeight, 1, kMaximumTileSide);
}

QVector<int> distributeRowHeights(const QVector<int>& rowUnits, int availableHeight) {
    int totalUnits = 0;
    for (int units : rowUnits) {
        totalUnits += qMax(1, units);
    }

    const int baseUnitHeight = qMax(1, availableHeight / qMax(1, totalUnits));
    int remainingPixels = availableHeight - baseUnitHeight * totalUnits;
    QVector<int> rowHeights;
    rowHeights.reserve(rowUnits.size());
    for (int units : rowUnits) {
        units = qMax(1, units);
        const int extra = qBound(0, remainingPixels, units);
        rowHeights.append(units * baseUnitHeight + extra);
        remainingPixels -= extra;
    }
    return rowHeights;
}

int densestRow(const QVector<int>& imageCounts, const QVector<int>& rowUnits) {
    int bestRow = 0;
    for (int row = 1; row < imageCounts.size(); ++row) {
        const qint64 left = static_cast<qint64>(qMax(0, imageCounts.at(row))) *
                            qMax(1, rowUnits.at(bestRow));
        const qint64 right = static_cast<qint64>(qMax(0, imageCounts.at(bestRow))) *
                             qMax(1, rowUnits.at(row));
        if (left > right ||
            (left == right && imageCounts.at(row) > imageCounts.at(bestRow))) {
            bestRow = row;
        }
    }
    return bestRow;
}
} // namespace

QRect TierRowGrid::tileRect(int itemIndex) const {
    if (itemIndex < 0) {
        return {};
    }
    const int safeColumns = qMax(1, columns);
    const int line = itemIndex / safeColumns;
    const int column = itemIndex % safeColumns;
    const int step = qMax(1, tileSide + kTileSpacing);
    return QRect(contentRect.left() + kTileMargin + column * step,
                 contentRect.top() + line * qMax(1, lineHeight), tileSide, tileSide);
}

int TierRowGrid::requiredRows(int itemCount) const {
    return qMax(1, (qMax(0, itemCount) + qMax(1, columns) - 1) / qMax(1, columns));
}

int TierRowGrid::insertionIndex(const QPoint& point, int itemCount) const {
    itemCount = qMax(0, itemCount);
    const int safeColumns = qMax(1, columns);
    const int totalSlots = itemCount + 1;
    const int maxLine = qMax(0, (totalSlots - 1) / safeColumns);
    const int line = qBound(0, (point.y() - contentRect.top()) / qMax(1, lineHeight), maxLine);

    const int step = qMax(1, tileSide + kTileSpacing);
    const int relativeX = point.x() - contentRect.left() - kTileMargin;
    int column = 0;
    if (relativeX > 0) {
        column = relativeX / step;
        if (relativeX - column * step > tileSide / 2) {
            ++column;
        }
    }
    column = qBound(0, column, safeColumns);
    return qBound(0, line * safeColumns + column, itemCount);
}

TierRowGrid TierListLayout::gridForRow(const QRect& rowRect, int rowUnits, int labelWidth) {
    rowUnits = qMax(1, rowUnits);
    labelWidth = qMax(0, labelWidth);
    const int lineHeight = qMax(1, rowRect.height() / rowUnits);
    const QRect contentRect(rowRect.left() + labelWidth + kContentLeadingBorder, rowRect.top(),
                            qMax(1, rowRect.width() - labelWidth - kContentLeadingBorder -
                                        kContentTrailingBorder),
                            rowRect.height());
    const int availableWidth = qMax(1, contentRect.width() - kTileMargin * 2);
    const int tileSide = qMax(1, qMin(tileSideForLineHeight(lineHeight), availableWidth));
    const int columns =
        qMax(1, (availableWidth + kTileSpacing) / (tileSide + kTileSpacing));
    return TierRowGrid{contentRect, lineHeight, tileSide, columns};
}

int TierListLayout::requiredRowUnits(int imageCount, int rowWidth, int lineHeight,
                                     int labelWidth) {
    const TierRowGrid grid = gridForRow(QRect(0, 0, qMax(1, rowWidth), qMax(1, lineHeight)), 1,
                                            labelWidth);
    return grid.requiredRows(imageCount);
}

TierBoardLayoutMetrics TierListLayout::fitBoard(const QVector<int>& imageCounts,
                                                 const QSize& viewportSize, int labelWidth) {
    TierBoardLayoutMetrics result;
    if (imageCounts.isEmpty()) {
        return result;
    }

    const int rowWidth = qMax(1, viewportSize.width());
    const int availableHeight = qMax(1, viewportSize.height());
    int maximumTotalUnits = 0;
    for (int imageCount : imageCounts) {
        maximumTotalUnits += qMax(1, imageCount);
    }

    const int minimumTotalUnits = imageCounts.size();
    for (int totalUnits = minimumTotalUnits; totalUnits <= maximumTotalUnits; ++totalUnits) {
        // A distributed row can receive one remainder pixel per unit, so use the ceiling as the
        // worst (largest) tile side when proving that every row fits.
        const int maximumLineHeight =
            qMax(1, (availableHeight + totalUnits - 1) / totalUnits);
        QVector<int> rowUnits;
        rowUnits.reserve(imageCounts.size());
        int requiredTotalUnits = 0;
        for (int row = 0; row < imageCounts.size(); ++row) {
            const int required = requiredRowUnits(imageCounts.at(row), rowWidth,
                                                  maximumLineHeight, labelWidth);
            rowUnits.append(required);
            requiredTotalUnits += required;
        }
        if (requiredTotalUnits > totalUnits) {
            continue;
        }

        // Spare vertical units go to the densest rows. This keeps the board balanced without
        // changing the fit proof, because the total unit count and maximum line height stay fixed.
        while (requiredTotalUnits < totalUnits) {
            ++rowUnits[densestRow(imageCounts, rowUnits)];
            ++requiredTotalUnits;
        }
        result.rowUnits = std::move(rowUnits);
        result.rowHeights = distributeRowHeights(result.rowUnits, availableHeight);
        return result;
    }

    return result;
}

} // namespace tlm
