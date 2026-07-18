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

    view.resize(854, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_COMPARE(view.size(), QSize(854, 650));
    QTRY_VERIFY(view.viewport()->width() > 0 && view.viewport()->height() > 0);
    const QSize normalViewportSize = view.viewport()->size();
    const TierBoardLayoutMetrics normal =
        TierListLayout::fitBoard({6, 6, 7, 31, 12}, normalViewportSize,
                                 delegate.labelWidth());
    QTRY_COMPARE(model.rowUnitCountAt(3), normal.rowUnits.at(3));
    QTRY_COMPARE(model.rowUnitCountAt(4), normal.rowUnits.at(4));

    view.resize(1090, 650);
    QTRY_COMPARE(view.size(), QSize(1090, 650));
    QTRY_VERIFY(view.viewport()->width() > normalViewportSize.width());
    const TierBoardLayoutMetrics collapsed =
        TierListLayout::fitBoard({6, 6, 7, 31, 12}, view.viewport()->size(),
                                 delegate.labelWidth());
    QTRY_COMPARE(model.rowUnitCountAt(3), collapsed.rowUnits.at(3));
    QTRY_COMPARE(model.rowUnitCountAt(4), collapsed.rowUnits.at(4));
    QVERIFY(normal.rowUnits != collapsed.rowUnits);
}

QTEST_MAIN(TierListLayoutTest)

#include "tst_TierListLayout.moc"
