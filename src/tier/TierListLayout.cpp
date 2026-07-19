#include "tier/TierListLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

struct MissionPlacement {
    int inputIndex{-1};
    QRectF imageRect;
};

struct MissionPackingCandidate {
    QVector<MissionPlacement> placements;
    qreal scale{0.0};
    qreal imageAreaOccupancy{0.0};
    qreal horizontalOccupancy{0.0};
    qreal verticalOccupancy{0.0};
    qreal density{0.0};
    qreal centerCompactness{0.0};
    qreal score{-std::numeric_limits<qreal>::infinity()};

    bool isValid() const {
        return !placements.isEmpty();
    }
};

bool rectContainsRect(const QRectF& outer, const QRectF& inner) {
    constexpr qreal kEpsilon = 0.25;
    const qreal outerRight = outer.left() + outer.width();
    const qreal outerBottom = outer.top() + outer.height();
    const qreal innerRight = inner.left() + inner.width();
    const qreal innerBottom = inner.top() + inner.height();
    return outer.left() <= inner.left() + kEpsilon && outer.top() <= inner.top() + kEpsilon &&
           outerRight + kEpsilon >= innerRight && outerBottom + kEpsilon >= innerBottom;
}

QSizeF normalizedSourceSize(const QSizeF& size) {
    return QSizeF(qMax<qreal>(1.0, size.width()), qMax<qreal>(1.0, size.height()));
}

qreal clampLayoutCoordinate(qreal minimum, qreal value, qreal maximum) {
    return maximum < minimum ? (minimum + maximum) / 2.0 : qBound(minimum, value, maximum);
}

class MissionMaxRectsPacker {
public:
    explicit MissionMaxRectsPacker(const QRectF& bounds)
        : m_bounds(bounds), m_center(bounds.center()) {
        m_freeRects.append(bounds);
    }

    QRectF insert(const QSizeF& size) {
        constexpr qreal kEpsilon = 0.25;
        int bestIndex = -1;
        QRectF bestRect;
        qreal bestCenterScore = std::numeric_limits<qreal>::max();
        qreal bestShortScore = std::numeric_limits<qreal>::max();
        qreal bestAreaScore = std::numeric_limits<qreal>::max();

        for (int index = 0; index < m_freeRects.size(); ++index) {
            const QRectF freeRect = m_freeRects.at(index);
            if (size.width() > freeRect.width() || size.height() > freeRect.height()) {
                continue;
            }

            const qreal x =
                clampLayoutCoordinate(freeRect.left(), m_center.x() - size.width() / 2.0,
                                      freeRect.right() - size.width());
            const qreal y =
                clampLayoutCoordinate(freeRect.top(), m_center.y() - size.height() / 2.0,
                                      freeRect.bottom() - size.height());
            const QRectF candidate(x, y, size.width(), size.height());
            const QPointF delta = candidate.center() - m_center;
            const qreal centerScore = delta.x() * delta.x() + delta.y() * delta.y();
            const qreal shortScore =
                qMin(freeRect.width() - size.width(), freeRect.height() - size.height());
            const qreal areaScore =
                freeRect.width() * freeRect.height() - size.width() * size.height();

            if (centerScore < bestCenterScore - kEpsilon ||
                (qAbs(centerScore - bestCenterScore) <= kEpsilon &&
                 (shortScore < bestShortScore - kEpsilon ||
                  (qAbs(shortScore - bestShortScore) <= kEpsilon && areaScore < bestAreaScore)))) {
                bestIndex = index;
                bestRect = candidate;
                bestCenterScore = centerScore;
                bestShortScore = shortScore;
                bestAreaScore = areaScore;
            }
        }

        if (bestIndex < 0) {
            return {};
        }

        splitFreeRects(bestRect);
        pruneFreeRects();
        return bestRect;
    }

private:
    void splitFreeRects(const QRectF& usedRect) {
        QVector<QRectF> nextFreeRects;
        nextFreeRects.reserve(m_freeRects.size() * 2);

        for (const QRectF& freeRect : std::as_const(m_freeRects)) {
            if (!freeRect.intersects(usedRect)) {
                nextFreeRects.append(freeRect);
                continue;
            }

            const QRectF intersection = freeRect.intersected(usedRect);
            if (!intersection.isValid() || intersection.isEmpty()) {
                nextFreeRects.append(freeRect);
                continue;
            }

            appendFreeRect(nextFreeRects,
                           QRectF(freeRect.left(), freeRect.top(),
                                  usedRect.left() - freeRect.left(), freeRect.height()));
            appendFreeRect(nextFreeRects,
                           QRectF(usedRect.right(), freeRect.top(),
                                  freeRect.right() - usedRect.right(), freeRect.height()));
            appendFreeRect(nextFreeRects,
                           QRectF(freeRect.left(), freeRect.top(), freeRect.width(),
                                  usedRect.top() - freeRect.top()));
            appendFreeRect(nextFreeRects,
                           QRectF(freeRect.left(), usedRect.bottom(), freeRect.width(),
                                  freeRect.bottom() - usedRect.bottom()));
        }

        m_freeRects = std::move(nextFreeRects);
    }

    void appendFreeRect(QVector<QRectF>& rects, const QRectF& rect) const {
        if (rect.width() < 1.0 || rect.height() < 1.0) {
            return;
        }
        const QRectF clipped = rect.intersected(m_bounds);
        if (clipped.width() >= 1.0 && clipped.height() >= 1.0) {
            rects.append(clipped);
        }
    }

    void pruneFreeRects() {
        QVector<QRectF> pruned;
        pruned.reserve(m_freeRects.size());
        for (int index = 0; index < m_freeRects.size(); ++index) {
            bool contained = false;
            for (int other = 0; other < m_freeRects.size(); ++other) {
                if (index != other &&
                    rectContainsRect(m_freeRects.at(other), m_freeRects.at(index))) {
                    contained = true;
                    break;
                }
            }
            if (!contained) {
                pruned.append(m_freeRects.at(index));
            }
        }
        m_freeRects = std::move(pruned);
    }

    QRectF m_bounds;
    QPointF m_center;
    QVector<QRectF> m_freeRects;
};

QVector<MissionPlacement> packMissionTilesAtScale(const QVector<QSizeF>& sourceSizes,
                                                  const QRectF& bounds, qreal gap, qreal scale) {
    QVector<int> order;
    order.reserve(sourceSizes.size());
    for (int index = 0; index < sourceSizes.size(); ++index) {
        order.append(index);
    }
    std::stable_sort(order.begin(), order.end(), [&sourceSizes](int left, int right) {
        const QSizeF leftSize = sourceSizes.at(left);
        const QSizeF rightSize = sourceSizes.at(right);
        const qreal leftArea = leftSize.width() * leftSize.height();
        const qreal rightArea = rightSize.width() * rightSize.height();
        return qFuzzyCompare(leftArea + 1.0, rightArea + 1.0) ? left < right
                                                              : leftArea > rightArea;
    });

    MissionMaxRectsPacker packer(bounds);
    QVector<MissionPlacement> placements;
    placements.reserve(sourceSizes.size());
    for (int inputIndex : std::as_const(order)) {
        const QSizeF sourceSize = normalizedSourceSize(sourceSizes.at(inputIndex));
        const QSizeF imageSize(qMax<qreal>(1.0, sourceSize.width() * scale),
                               qMax<qreal>(1.0, sourceSize.height() * scale));
        const QSizeF paddedSize(imageSize.width() + gap, imageSize.height() + gap);
        const QRectF paddedRect = packer.insert(paddedSize);
        if (!paddedRect.isValid() || paddedRect.isEmpty()) {
            return {};
        }
        placements.append(MissionPlacement{
            inputIndex,
            paddedRect.adjusted(gap / 2.0, gap / 2.0, -gap / 2.0, -gap / 2.0),
        });
    }
    return placements;
}

QRectF missionPlacementBounds(const QVector<MissionPlacement>& placements) {
    if (placements.isEmpty()) {
        return {};
    }
    QRectF groupRect = placements.constFirst().imageRect;
    for (const MissionPlacement& placement : placements) {
        groupRect = groupRect.united(placement.imageRect);
    }
    return groupRect;
}

void centerMissionPlacements(QVector<MissionPlacement>& placements, const QRectF& bounds) {
    const QRectF groupRect = missionPlacementBounds(placements);
    if (!groupRect.isValid()) {
        return;
    }
    const QPointF delta = bounds.center() - groupRect.center();
    for (MissionPlacement& placement : placements) {
        placement.imageRect.translate(delta);
    }
}

bool intervalsNeedSeparation(qreal firstStart, qreal firstEnd, qreal secondStart,
                             qreal secondEnd, qreal gap) {
    constexpr qreal kEpsilon = 0.01;
    return firstStart < secondEnd + gap - kEpsilon &&
           secondStart < firstEnd + gap - kEpsilon;
}

qreal horizontalTravelTowardCenter(const QVector<MissionPlacement>& placements, int movingIndex,
                                   const QRectF& bounds, qreal gap, qreal desiredTravel) {
    const QRectF moving = placements.at(movingIndex).imageRect;
    qreal available = desiredTravel > 0.0 ? bounds.right() - moving.right()
                                           : moving.left() - bounds.left();
    for (int index = 0; index < placements.size(); ++index) {
        if (index == movingIndex) {
            continue;
        }
        const QRectF blocker = placements.at(index).imageRect;
        if (!intervalsNeedSeparation(moving.top(), moving.bottom(), blocker.top(),
                                     blocker.bottom(), gap)) {
            continue;
        }
        if (desiredTravel > 0.0 && blocker.left() >= moving.right()) {
            available = qMin(available, blocker.left() - gap - moving.right());
        } else if (desiredTravel < 0.0 && blocker.right() <= moving.left()) {
            available = qMin(available, moving.left() - gap - blocker.right());
        }
    }
    return qMin(qAbs(desiredTravel), qMax<qreal>(0.0, available));
}

qreal verticalTravelTowardCenter(const QVector<MissionPlacement>& placements, int movingIndex,
                                 const QRectF& bounds, qreal gap, qreal desiredTravel) {
    const QRectF moving = placements.at(movingIndex).imageRect;
    qreal available = desiredTravel > 0.0 ? bounds.bottom() - moving.bottom()
                                           : moving.top() - bounds.top();
    for (int index = 0; index < placements.size(); ++index) {
        if (index == movingIndex) {
            continue;
        }
        const QRectF blocker = placements.at(index).imageRect;
        if (!intervalsNeedSeparation(moving.left(), moving.right(), blocker.left(),
                                     blocker.right(), gap)) {
            continue;
        }
        if (desiredTravel > 0.0 && blocker.top() >= moving.bottom()) {
            available = qMin(available, blocker.top() - gap - moving.bottom());
        } else if (desiredTravel < 0.0 && blocker.bottom() <= moving.top()) {
            available = qMin(available, moving.top() - gap - blocker.bottom());
        }
    }
    return qMin(qAbs(desiredTravel), qMax<qreal>(0.0, available));
}

void compactMissionPlacementsTowardCenter(QVector<MissionPlacement>& placements,
                                          const QRectF& bounds, qreal gap) {
    centerMissionPlacements(placements, bounds);
    QVector<int> order;
    order.reserve(placements.size());
    for (int index = 0; index < placements.size(); ++index) {
        order.append(index);
    }

    // Coordinate descent monotonically reduces every rectangle's distance to the board center.
    // Collision limits apply the same gap on both axes, so no row-specific rules are needed.
    constexpr int kMaximumPasses = 8;
    for (int pass = 0; pass < kMaximumPasses; ++pass) {
        std::sort(order.begin(), order.end(), [&placements, &bounds](int left, int right) {
            const QPointF leftDelta = placements.at(left).imageRect.center() - bounds.center();
            const QPointF rightDelta = placements.at(right).imageRect.center() - bounds.center();
            return leftDelta.x() * leftDelta.x() + leftDelta.y() * leftDelta.y() <
                   rightDelta.x() * rightDelta.x() + rightDelta.y() * rightDelta.y();
        });

        qreal totalTravel = 0.0;
        for (int index : std::as_const(order)) {
            QRectF& rect = placements[index].imageRect;
            const qreal desiredX = bounds.center().x() - rect.center().x();
            const qreal horizontalTravel = horizontalTravelTowardCenter(
                placements, index, bounds, gap, desiredX);
            rect.translate(std::copysign(horizontalTravel, desiredX), 0.0);

            const qreal desiredY = bounds.center().y() - rect.center().y();
            const qreal verticalTravel = verticalTravelTowardCenter(
                placements, index, bounds, gap, desiredY);
            rect.translate(0.0, std::copysign(verticalTravel, desiredY));
            totalTravel += horizontalTravel + verticalTravel;
        }
        if (totalTravel < 0.1) {
            break;
        }
    }
    centerMissionPlacements(placements, bounds);
}

QRectF centeredBoundsForAspect(const QRectF& bounds, qreal aspect) {
    aspect = qMax<qreal>(std::numeric_limits<qreal>::epsilon(), aspect);
    QSizeF size = bounds.size();
    if (size.width() / size.height() > aspect) {
        size.setWidth(size.height() * aspect);
    } else {
        size.setHeight(size.width() / aspect);
    }
    return QRectF(bounds.center() - QPointF(size.width() / 2.0, size.height() / 2.0), size);
}

MissionPackingCandidate packMissionCandidate(const QVector<QSizeF>& sourceSizes,
                                             const QRectF& packingBounds,
                                             const QRectF& displayBounds, qreal gap) {
    MissionPackingCandidate candidate;
    qreal sourceArea = 0.0;
    for (const QSizeF& size : sourceSizes) {
        const QSizeF normalized = normalizedSourceSize(size);
        sourceArea += normalized.width() * normalized.height();
    }

    const qreal packingArea = qMax<qreal>(1.0, packingBounds.width() * packingBounds.height());
    const qreal fillTarget = sourceSizes.size() <= 2 ? 0.54 : 0.84;
    const qreal areaDrivenScale =
        std::sqrt((packingArea * fillTarget) / qMax<qreal>(1.0, sourceArea));
    qreal low = 0.0;
    qreal high = qBound<qreal>(0.08, areaDrivenScale, 2.35);
    constexpr int kSearchSteps = 26;
    for (int step = 0; step < kSearchSteps; ++step) {
        const qreal middle = (low + high) / 2.0;
        QVector<MissionPlacement> placements =
            packMissionTilesAtScale(sourceSizes, packingBounds, gap, middle);
        if (placements.size() == sourceSizes.size()) {
            candidate.placements = std::move(placements);
            candidate.scale = middle;
            low = middle;
        } else {
            high = middle;
        }
    }
    if (!candidate.isValid()) {
        return candidate;
    }

    compactMissionPlacementsTowardCenter(candidate.placements, displayBounds, gap);
    const QRectF groupRect = missionPlacementBounds(candidate.placements);
    const qreal displayArea = qMax<qreal>(1.0, displayBounds.width() * displayBounds.height());
    qreal imageArea = 0.0;
    for (const MissionPlacement& placement : candidate.placements) {
        imageArea += placement.imageRect.width() * placement.imageRect.height();
    }

    candidate.imageAreaOccupancy = qBound<qreal>(0.0, imageArea / displayArea, 1.0);
    candidate.horizontalOccupancy =
        qBound<qreal>(0.0, groupRect.width() / displayBounds.width(), 1.0);
    candidate.verticalOccupancy =
        qBound<qreal>(0.0, groupRect.height() / displayBounds.height(), 1.0);
    const qreal groupArea = qMax<qreal>(1.0, groupRect.width() * groupRect.height());
    candidate.density = qBound<qreal>(0.0, imageArea / groupArea, 1.0);

    qreal weightedDistance = 0.0;
    for (const MissionPlacement& placement : candidate.placements) {
        const qreal area = placement.imageRect.width() * placement.imageRect.height();
        const QPointF delta = placement.imageRect.center() - displayBounds.center();
        const qreal normalizedX = delta.x() / qMax<qreal>(1.0, displayBounds.width() / 2.0);
        const qreal normalizedY = delta.y() / qMax<qreal>(1.0, displayBounds.height() / 2.0);
        weightedDistance += area * (normalizedX * normalizedX + normalizedY * normalizedY);
    }
    candidate.centerCompactness =
        1.0 / (1.0 + std::sqrt(weightedDistance / qMax<qreal>(1.0, imageArea)));

    const qreal weakerAxis =
        qMin(candidate.horizontalOccupancy, candidate.verticalOccupancy);
    const qreal axisDifference =
        qAbs(candidate.horizontalOccupancy - candidate.verticalOccupancy);
    candidate.score = std::sqrt(candidate.imageAreaOccupancy) * 0.25 + weakerAxis * 0.20 +
                      (1.0 - axisDifference) * 0.15 + candidate.density * 0.20 +
                      candidate.centerCompactness * 0.20;
    return candidate;
}

MissionPackingCandidate balancedMissionPacking(const QVector<QSizeF>& sourceSizes,
                                               const QRectF& bounds, qreal gap) {
    const qreal boundsAspect = bounds.width() / bounds.height();
    MissionPackingCandidate best =
        packMissionCandidate(sourceSizes, bounds, bounds, gap);
    if (!best.isValid()) {
        return best;
    }

    const qreal axisDifference =
        qAbs(best.horizontalOccupancy - best.verticalOccupancy);
    if (axisDifference <= 0.04) {
        return best;
    }

    // Aim the packing bounds toward the under-filled axis. The geometric midpoint and bounded
    // correction capture useful MaxRects topology changes without a broad resize-time search.
    const qreal correction =
        qBound<qreal>(0.65, best.verticalOccupancy / best.horizontalOccupancy, 1.55);
    const qreal aspectMultipliers[]{std::sqrt(correction), correction};
    for (qreal multiplier : aspectMultipliers) {
        const QRectF packingBounds = centeredBoundsForAspect(bounds, boundsAspect * multiplier);
        MissionPackingCandidate alternative =
            packMissionCandidate(sourceSizes, packingBounds, bounds, gap);
        if (alternative.isValid() &&
            (alternative.score > best.score + 0.0001 ||
             (qAbs(alternative.score - best.score) <= 0.0001 &&
              alternative.imageAreaOccupancy > best.imageAreaOccupancy))) {
            best = std::move(alternative);
        }
    }
    return best;
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

MissionControlLayoutMetrics TierListLayout::fitMissionControl(
    const QVector<QSizeF>& sourceSizes, const QRectF& bounds, qreal gap) {
    MissionControlLayoutMetrics result;
    if (sourceSizes.isEmpty() || !bounds.isValid() || bounds.isEmpty()) {
        return result;
    }

    QVector<QSizeF> normalizedSizes;
    normalizedSizes.reserve(sourceSizes.size());
    for (const QSizeF& size : sourceSizes) {
        normalizedSizes.append(normalizedSourceSize(size));
    }

    const MissionPackingCandidate packing =
        balancedMissionPacking(normalizedSizes, bounds, qMax<qreal>(0.0, gap));
    if (!packing.isValid() || packing.placements.size() != normalizedSizes.size()) {
        return result;
    }

    result.itemRects.resize(normalizedSizes.size());
    for (const MissionPlacement& placement : packing.placements) {
        if (placement.inputIndex < 0 || placement.inputIndex >= result.itemRects.size()) {
            return {};
        }
        result.itemRects[placement.inputIndex] = placement.imageRect;
    }
    result.scale = packing.scale;
    result.imageAreaOccupancy = packing.imageAreaOccupancy;
    result.horizontalOccupancy = packing.horizontalOccupancy;
    result.verticalOccupancy = packing.verticalOccupancy;
    return result;
}

} // namespace tlm
