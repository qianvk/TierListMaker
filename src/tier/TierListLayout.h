#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

namespace tlm {

/** Shared geometry for one tier row. Painting and hit testing must use this exact grid. */
struct TierRowGrid {
    QRect contentRect;
    int lineHeight{1};
    int tileSide{1};
    int columns{1};

    QRect tileRect(int itemIndex) const;
    int requiredRows(int itemCount) const;
    int insertionIndex(const QPoint& point, int itemCount) const;
};

struct TierBoardLayoutMetrics {
    QVector<int> rowHeights;
    QVector<int> rowUnits;
};

/** Computes a non-scrolling board layout that keeps every tile inside its tier row. */
class TierListLayout final {
public:
    static TierRowGrid gridForRow(const QRect& rowRect, int rowUnits, int labelWidth);
    static int requiredRowUnits(int imageCount, int rowWidth, int lineHeight, int labelWidth);
    static TierBoardLayoutMetrics fitBoard(const QVector<int>& imageCounts,
                                           const QSize& viewportSize, int labelWidth);
};

} // namespace tlm
