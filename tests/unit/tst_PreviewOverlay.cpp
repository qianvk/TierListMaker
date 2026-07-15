#include "preview/PreviewOverlay.h"

#include "settings/AppSettings.h"

#include <QPushButton>
#include <QSignalSpy>
#include <QWindow>
#include <QtTest>

#include <QWKWidgets/widgetwindowagent.h>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

using namespace tlm;

class PreviewOverlayTest final : public QObject {
    Q_OBJECT

private slots:
    void blocksUnderlyingInputUntilCloseFinishes();
    void doubleClickingPreviewImageCloses();
    void clickingOutsidePreviewImageCloses();
    void windowChromeDoesNotStealPreviewInput();
};

#if defined(Q_OS_WIN)
namespace {
LRESULT hitTestAt(QWidget& host, const QPoint& clientPosition) {
    const HWND hwnd = reinterpret_cast<HWND>(host.winId());
    POINT nativePosition{clientPosition.x(), clientPosition.y()};
    ::ClientToScreen(hwnd, &nativePosition);
    return ::SendMessageW(hwnd, WM_NCHITTEST, 0,
                          MAKELPARAM(nativePosition.x, nativePosition.y));
}

} // namespace
#endif

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
    QCOMPARE(QWidget::mouseGrabber(), &overlay);
    QCOMPARE(QWidget::keyboardGrabber(), &overlay);

    const QPoint imageCenter = overlay.previewGeometry().center();
    QCOMPARE(overlay.toolTipTextAt(imageCenter),
             QStringLiteral("Double-click image to close"));
#if defined(Q_OS_WIN)
    QCOMPARE(hitTestAt(host, imageCenter), static_cast<LRESULT>(HTCLIENT));
#endif
    QTest::mouseDClick(host.windowHandle(), Qt::LeftButton, Qt::NoModifier, imageCenter);

    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());
}

void PreviewOverlayTest::windowChromeDoesNotStealPreviewInput() {
#if defined(Q_OS_WIN)
    QWidget host;
    host.resize(800, 600);
    QWidget titleBar(&host);
    titleBar.setGeometry(0, 0, host.width(), 44);

    QWK::WidgetWindowAgent agent;
    agent.setResizable(true);
    QVERIFY(agent.setup(&host));
    QVERIFY(agent.addTitleBar(&titleBar));

    PreviewOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    QVERIFY(agent.setHitTestVisible(&titleBar, &overlay, true));

    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPixmap image(640, 360);
    image.fill(QColor(68, 126, 214));
    QSignalSpy closedSpy(&overlay, &PreviewOverlay::closed);
    overlay.openPreview(QRect(220, 180, 72, 72), image);

    const QPoint titleBarPoint(120, 22);
    QCOMPARE(hitTestAt(host, titleBarPoint), static_cast<LRESULT>(HTCLIENT));
    QCOMPARE(QWidget::mouseGrabber(), &overlay);
    QTest::mouseClick(host.windowHandle(), Qt::LeftButton, Qt::NoModifier, titleBarPoint);

    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());
#else
    QSKIP("The non-client title-bar regression is specific to Windows.");
#endif
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
    QTest::mouseClick(host.windowHandle(), Qt::LeftButton, Qt::NoModifier, outsidePoint);

    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 1000);
    QVERIFY(!overlay.isOpen());
}

QTEST_MAIN(PreviewOverlayTest)

#include "tst_PreviewOverlay.moc"
