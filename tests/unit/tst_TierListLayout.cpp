#include "tier/TierListLayout.h"
#include "tier/TierListDelegate.h"
#include "tier/TierListModel.h"
#include "tier/TierListView.h"

#include <QtTest>

#include <utility>

using namespace tlm;

class TierListLayoutTest final : public QObject {
    Q_OBJECT

private slots:
    void girlsProjectKeepsEveryImageInsideItsRow_data();
    void girlsProjectKeepsEveryImageInsideItsRow();
    void expandedWidthsReflowRows();
    void viewResizeRecomputesLayout();
    void missionControlBalancesCenteredFreePacking();
};

namespace {
TierProject girlsProject() {
    TierProject project;
    const QVector<int> imageCounts{6, 6, 7, 31, 12};
    const QStringList labels{QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("B"),
                             QStringLiteral("C"), QStringLiteral("D")};
    for (int row = 0; row < imageCounts.size(); ++row) {
        TierRow tierRow;
        tierRow.id = QStringLiteral("row-%1").arg(row);
        tierRow.label = labels.at(row);
        tierRow.order = row;
        for (int image = 0; image < imageCounts.at(row); ++image) {
            tierRow.imageIds.append(QStringLiteral("image-%1-%2").arg(row).arg(image));
        }
        project.rows.append(std::move(tierRow));
    }
    return project;
}
} // namespace

void TierListLayoutTest::girlsProjectKeepsEveryImageInsideItsRow_data() {
    QTest::addColumn<QSize>("viewportSize");
    QTest::newRow("normal-sidebar") << QSize(854, 650);
    QTest::newRow("collapsed-sidebar-threshold") << QSize(1090, 650);
    QTest::newRow("focus-mode") << QSize(1148, 780);
    QTest::newRow("minimum-window") << QSize(700, 550);
}

void TierListLayoutTest::girlsProjectKeepsEveryImageInsideItsRow() {
    QFETCH(QSize, viewportSize);
    const QVector<int> imageCounts{6, 6, 7, 31, 12};
    constexpr int labelWidth = 82;

    const TierBoardLayoutMetrics layout =
        TierListLayout::fitBoard(imageCounts, viewportSize, labelWidth);
    QCOMPARE(layout.rowHeights.size(), imageCounts.size());
    QCOMPARE(layout.rowUnits.size(), imageCounts.size());

    int rowTop = 0;
    for (int row = 0; row < imageCounts.size(); ++row) {
        const QRect rowRect(0, rowTop, viewportSize.width(), layout.rowHeights.at(row));
        const TierRowGrid grid =
            TierListLayout::gridForRow(rowRect, layout.rowUnits.at(row), labelWidth);
        QVERIFY2(grid.requiredRows(imageCounts.at(row)) <= layout.rowUnits.at(row),
                 "The board allocated fewer lines than its delegate grid requires.");
        if (imageCounts.at(row) > 0) {
            const QRect lastTile = grid.tileRect(imageCounts.at(row) - 1);
            QVERIFY2(rowRect.contains(lastTile.topLeft()) && rowRect.contains(lastTile.bottomRight()),
                     "The last image falls outside the tier row and would be clipped.");
        }
        rowTop += rowRect.height();
    }
    QCOMPARE(rowTop, viewportSize.height());
}

void TierListLayoutTest::expandedWidthsReflowRows() {
    const QVector<int> imageCounts{6, 6, 7, 31, 12};
    const TierBoardLayoutMetrics normal =
        TierListLayout::fitBoard(imageCounts, QSize(854, 650), 82);
    const TierBoardLayoutMetrics collapsed =
        TierListLayout::fitBoard(imageCounts, QSize(1090, 650), 82);

    QVERIFY(normal.rowUnits != collapsed.rowUnits);
    QVERIFY(collapsed.rowUnits.at(3) <= normal.rowUnits.at(3));
}

void TierListLayoutTest::viewResizeRecomputesLayout() {
    TierProject project = girlsProject();
    TierListModel model;
    TierListDelegate delegate;
    TierListView view;
    delegate.setContext(&project, nullptr, nullptr, {});
    view.setModel(&model);
    view.setItemDelegate(&delegate);
    model.setProject(&project);

    // Reflow only needs widget events; native exposure is nondeterministic on headless CI runners.
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(854, 650);
    view.show();
    QCoreApplication::processEvents();
    QTRY_VERIFY(view.viewport()->width() > 0 && view.viewport()->height() > 0);
    const QSize normalViewportSize = view.viewport()->size();
    const TierBoardLayoutMetrics normal =
        TierListLayout::fitBoard({6, 6, 7, 31, 12}, normalViewportSize,
                                 delegate.labelWidth());
    QTRY_COMPARE(model.rowUnitCountAt(3), normal.rowUnits.at(3));
    QTRY_COMPARE(model.rowUnitCountAt(4), normal.rowUnits.at(4));

    view.resize(1090, 650);
    QTRY_VERIFY(view.viewport()->width() > normalViewportSize.width());
    const TierBoardLayoutMetrics collapsed =
        TierListLayout::fitBoard({6, 6, 7, 31, 12}, view.viewport()->size(),
                                 delegate.labelWidth());
    QTRY_COMPARE(model.rowUnitCountAt(3), collapsed.rowUnits.at(3));
    QTRY_COMPARE(model.rowUnitCountAt(4), collapsed.rowUnits.at(4));
    QVERIFY(normal.rowUnits != collapsed.rowUnits);
}

void TierListLayoutTest::missionControlBalancesCenteredFreePacking() {
    const QVector<QSizeF> sourceSizes{
        {1000.0, 1500.0}, {1500.0, 2000.0}, {1600.0, 2000.0}, {1800.0, 2000.0},
        {2400.0, 2400.0}, {3600.0, 2000.0}, {3000.0, 2250.0}, {2400.0, 2000.0},
        {3000.0, 1800.0}, {2400.0, 1200.0},
    };
    const QRectF bounds(20.0, 30.0, 1200.0, 700.0);
    constexpr qreal gap = 12.0;
    constexpr qreal epsilon = 0.01;

    const MissionControlLayoutMetrics layout =
        TierListLayout::fitMissionControl(sourceSizes, bounds, gap);
    QCOMPARE(layout.itemRects.size(), sourceSizes.size());
    QVERIFY(layout.scale > 0.0);
    QVERIFY(qMin(layout.horizontalOccupancy, layout.verticalOccupancy) > 0.55);
    QVERIFY(qAbs(layout.horizontalOccupancy - layout.verticalOccupancy) < 0.30);
    const qreal packedDensity =
        layout.imageAreaOccupancy /
        (layout.horizontalOccupancy * layout.verticalOccupancy);
    QVERIFY(packedDensity > 0.50);

    QRectF groupRect;
    bool hasDifferentHeights = false;
    for (int index = 0; index < layout.itemRects.size(); ++index) {
        const QRectF rect = layout.itemRects.at(index);
        QVERIFY(rect.left() >= bounds.left() - epsilon);
        QVERIFY(rect.top() >= bounds.top() - epsilon);
        QVERIFY(rect.right() <= bounds.right() + epsilon);
        QVERIFY(rect.bottom() <= bounds.bottom() + epsilon);
        QVERIFY(qAbs(rect.width() / rect.height() -
                     sourceSizes.at(index).width() / sourceSizes.at(index).height()) <
                epsilon);
        QVERIFY(qAbs(rect.width() / sourceSizes.at(index).width() - layout.scale) < epsilon);
        QVERIFY(qAbs(rect.height() / sourceSizes.at(index).height() - layout.scale) < epsilon);
        if (index > 0 &&
            qAbs(rect.height() - layout.itemRects.constFirst().height()) > epsilon) {
            hasDifferentHeights = true;
        }
        groupRect = groupRect.isValid() ? groupRect.united(rect) : rect;
    }
    QVERIFY(hasDifferentHeights);
    QVERIFY(qAbs(groupRect.center().x() - bounds.center().x()) < epsilon);
    QVERIFY(qAbs(groupRect.center().y() - bounds.center().y()) < epsilon);

    for (int first = 0; first < layout.itemRects.size(); ++first) {
        for (int second = first + 1; second < layout.itemRects.size(); ++second) {
            const QRectF left = layout.itemRects.at(first);
            const QRectF right = layout.itemRects.at(second);
            const bool separated = left.right() + gap <= right.left() + epsilon ||
                                   right.right() + gap <= left.left() + epsilon ||
                                   left.bottom() + gap <= right.top() + epsilon ||
                                   right.bottom() + gap <= left.top() + epsilon;
            QVERIFY2(separated, "Mission Control images overlap or violate the fixed gap.");
        }
    }
}

QTEST_MAIN(TierListLayoutTest)

#include "tst_TierListLayout.moc"
