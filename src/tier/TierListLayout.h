#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
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

/** Centered free-form packing used by Mission Control without cropping source images. */
struct MissionControlLayoutMetrics {
    QVector<QRectF> itemRects;
    qreal scale{0.0};
    qreal imageAreaOccupancy{0.0};
    qreal horizontalOccupancy{0.0};
    qreal verticalOccupancy{0.0};
};

/** Computes a non-scrolling board layout that keeps every tile inside its tier row. */
class TierListLayout final {
public:
    static TierRowGrid gridForRow(const QRect& rowRect, int rowUnits, int labelWidth);
    static int requiredRowUnits(int imageCount, int rowWidth, int lineHeight, int labelWidth);
    static TierBoardLayoutMetrics fitBoard(const QVector<int>& imageCounts,
                                           const QSize& viewportSize, int labelWidth);
    static MissionControlLayoutMetrics fitMissionControl(const QVector<QSizeF>& sourceSizes,
                                                         const QRectF& bounds, qreal gap);
};

} // namespace tlm
