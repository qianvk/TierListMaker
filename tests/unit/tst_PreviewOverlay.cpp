#include "preview/PreviewOverlay.h"

#include "settings/AppSettings.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace tlm;

class PreviewOverlayTest final : public QObject {
    Q_OBJECT

private slots:
    void blocksUnderlyingInputUntilCloseFinishes();
    void doubleClickingPreviewImageCloses();
    void clickingOutsidePreviewImageCloses();
};

void PreviewOverlayTest::blocksUnderlyingInputUntilCloseFinishes() {
    QWidget host;
    host.resize(800, 600);

    QPushButton underlyingButton(QStringLiteral("Behind preview"), &host);
    underlyingButton.setGeometry(20, 20, 160, 36);
    int clickCount = 0;
    connect(&underlyingButton, &QPushButton::clicked, this, [&clickCount]() { ++clickCount; });

    PreviewOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    overlay.setBackgroundMode(PreviewBackgroundMode::SelfImage);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPixmap image(640, 360);
    image.fill(QColor(68, 126, 214));
    QSignalSpy openedSpy(&overlay, &PreviewOverlay::opened);
    QSignalSpy closedSpy(&overlay, &PreviewOverlay::closed);
    overlay.openPreview(QRect(220, 180, 72, 72), image);

    QCOMPARE(openedSpy.count(), 1);
    QVERIFY(overlay.isOpen());
    QCOMPARE(overlay.geometry(), host.rect());

    // Direct delivery simulates a leaked event. The application-level barrier must still stop it.
    QTest::mouseClick(&underlyingButton, Qt::LeftButton);
    QCOMPARE(clickCount, 0);

    QTest::keyClick(&underlyingButton, Qt::Key_Space);
    host.resize(760, 560);
    overlay.setGeometry(host.rect());
    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());

    QTest::mouseClick(&underlyingButton, Qt::LeftButton);
    QCOMPARE(clickCount, 1);
}

void PreviewOverlayTest::doubleClickingPreviewImageCloses() {
    QWidget host;
    host.resize(800, 600);
    PreviewOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPixmap image(640, 360);
    image.fill(QColor(68, 126, 214));
    QSignalSpy closedSpy(&overlay, &PreviewOverlay::closed);
    overlay.openPreview(QRect(220, 180, 72, 72), image);
    QTRY_VERIFY_WITH_TIMEOUT(overlay.previewGeometry().width() > 400, 1000);

    const QPoint imageCenter = overlay.previewGeometry().center();
    QCOMPARE(overlay.toolTipTextAt(imageCenter),
             QStringLiteral("Double-click image to close"));
    QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, imageCenter);

    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());
}

void PreviewOverlayTest::clickingOutsidePreviewImageCloses() {
    QWidget host;
    host.resize(800, 600);
    PreviewOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPixmap image(640, 360);
    image.fill(QColor(68, 126, 214));
    QSignalSpy closedSpy(&overlay, &PreviewOverlay::closed);
    overlay.openPreview(QRect(220, 180, 72, 72), image);

    const QPoint outsidePoint(8, 8);
    QCOMPARE(overlay.toolTipTextAt(outsidePoint), QStringLiteral("Click to close preview"));
    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, outsidePoint);

    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());
}

QTEST_MAIN(PreviewOverlayTest)

#include "tst_PreviewOverlay.moc"
