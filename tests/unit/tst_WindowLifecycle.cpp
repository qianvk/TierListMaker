#include "preview/PreviewOverlay.h"
#include "window/AppDialog.h"

#include <QApplication>
#include <QPointer>
#include <QPushButton>
#include <QtTest>

#include <QWKWidgets/widgetwindowagent.h>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

using namespace tlm;

class WindowLifecycleTest final : public QObject {
    Q_OBJECT

private slots:
    void dialogClosePreservesWindowInput();
};

#if defined(Q_OS_WIN)
namespace {

LPARAM screenPointParameter(const QPoint& screenPosition) {
    return MAKELPARAM(screenPosition.x(), screenPosition.y());
}

POINT nativeScreenPosition(QWidget& host, const QPoint& hostPosition) {
    const qreal scale = host.devicePixelRatioF();
    POINT position{qRound(hostPosition.x() * scale), qRound(hostPosition.y() * scale)};
    ::ClientToScreen(reinterpret_cast<HWND>(host.winId()), &position);
    return position;
}

LRESULT hitTestAt(QWidget& host, const QPoint& hostPosition) {
    const HWND hwnd = reinterpret_cast<HWND>(host.winId());
    const POINT screenPosition = nativeScreenPosition(host, hostPosition);
    return ::SendMessageW(hwnd, WM_NCHITTEST, 0,
                          MAKELPARAM(screenPosition.x, screenPosition.y));
}

QPoint findCaptionPoint(QWidget& host) {
    for (int y = 12; y < 40; y += 4) {
        for (int x = 120; x < host.width() - 180; x += 24) {
            if (hitTestAt(host, QPoint(x, y)) == HTCAPTION) {
                return QPoint(x, y);
            }
        }
    }
    return {};
}

void clickNativeCaptionClose(AppDialog& dialog) {
    auto* closeButton = dialog.findChild<QPushButton*>(QStringLiteral("qwkWindowsCloseButton"));
    QVERIFY(closeButton);

    const QPoint dialogPosition = closeButton->mapTo(&dialog, closeButton->rect().center());
    const POINT nativePosition = nativeScreenPosition(dialog, dialogPosition);
    const QPoint screenPosition(nativePosition.x, nativePosition.y);
    const HWND hwnd = reinterpret_cast<HWND>(dialog.winId());
    const LPARAM position = screenPointParameter(screenPosition);
    QCOMPARE(::SendMessageW(hwnd, WM_NCHITTEST, 0, position), static_cast<LRESULT>(HTCLOSE));
    QTest::mouseClick(closeButton, Qt::LeftButton);
}

void clickClientPoint(QWidget& host, const QPoint& hostPosition) {
    const HWND hwnd = reinterpret_cast<HWND>(host.winId());
    const qreal scale = host.devicePixelRatioF();
    POINT clientPosition{qRound(hostPosition.x() * scale), qRound(hostPosition.y() * scale)};
    const LPARAM position = MAKELPARAM(clientPosition.x, clientPosition.y);
    ::SendMessageW(hwnd, WM_MOUSEMOVE, 0, position);
    ::SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, position);
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, position);
}

} // namespace
#endif

void WindowLifecycleTest::dialogClosePreservesWindowInput() {
#if defined(Q_OS_WIN)
    QWidget host;
    host.resize(960, 640);
    QWidget sidebarTitleBar(&host);
    sidebarTitleBar.setGeometry(0, 0, 240, 44);
    QWidget contentTitleBar(&host);
    contentTitleBar.setGeometry(240, 0, host.width() - 240, 44);
    PreviewOverlay preview(&host);
    preview.setGeometry(host.rect());

    QWK::WidgetWindowAgent hostAgent;
    hostAgent.setResizable(true);
    QVERIFY(hostAgent.setup(&host));
    QVERIFY(hostAgent.installSystemButtons());
    QVERIFY(hostAgent.addTitleBar(&sidebarTitleBar));
    QVERIFY(hostAgent.addTitleBar(&contentTitleBar));
    QVERIFY(hostAgent.setHitTestVisible(&sidebarTitleBar, &preview, true));
    QVERIFY(hostAgent.setHitTestVisible(&contentTitleBar, &preview, true));

    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    const QPoint captionPoint = findCaptionPoint(host);
    QVERIFY(!captionPoint.isNull());
    QCOMPARE(hitTestAt(host, captionPoint), static_cast<LRESULT>(HTCAPTION));

    for (int cycle = 0; cycle < 12; ++cycle) {
        QPointer<AppDialog> dialog = new AppDialog(QStringLiteral("Lifecycle"), &host);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(520, 360);

        // Installing custom caption controls must not create the platform window before show().
        QVERIFY(!dialog->internalWinId());
        dialog->show();
        QTRY_VERIFY(qApp->activeModalWidget());
        QCOMPARE(qApp->activeModalWidget(), dialog.data());
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        QVERIFY(!::IsWindowEnabled(reinterpret_cast<HWND>(host.winId())));

        clickNativeCaptionClose(*dialog);
        QTRY_VERIFY(dialog.isNull());
        QTRY_VERIFY(!qApp->activeModalWidget());
        QTRY_VERIFY(::IsWindowEnabled(reinterpret_cast<HWND>(host.winId())));
        QVERIFY(!QWidget::mouseGrabber());
        QVERIFY(!QWidget::keyboardGrabber());
        QCOMPARE(hitTestAt(host, captionPoint), static_cast<LRESULT>(HTCAPTION));
    }

    QPixmap pixmap(240, 160);
    pixmap.fill(Qt::red);
    preview.openPreview(QRect(host.rect().center(), QSize(40, 30)), pixmap);
    QTRY_VERIFY(preview.isOpen());
    QCOMPARE(QWidget::mouseGrabber(), &preview);
    QCOMPARE(QWidget::keyboardGrabber(), &preview);

    const QPoint outsideInOverlay(8, preview.height() - 8);
    const QPoint outsideInHost = preview.mapTo(&host, outsideInOverlay);
    QCOMPARE(hitTestAt(host, outsideInHost), static_cast<LRESULT>(HTCLIENT));
    QVERIFY(!preview.toolTipTextAt(outsideInOverlay).isEmpty());

    clickClientPoint(host, outsideInHost);
    QTRY_VERIFY(!preview.isOpen());

    preview.openPreview(QRect(host.rect().center(), QSize(40, 30)), pixmap);
    QTRY_VERIFY(preview.isOpen());
    const QPoint imagePosition = preview.rect().center();
    QVERIFY(preview.toolTipTextAt(imagePosition) != preview.toolTipTextAt(outsideInOverlay));
    QTest::mouseDClick(&preview, Qt::LeftButton, Qt::NoModifier, imagePosition);
    QTRY_VERIFY(!preview.isOpen());
#else
    QSKIP("The native AppDialog lifecycle regression is specific to Windows.");
#endif
}

QTEST_MAIN(WindowLifecycleTest)

#include "tst_WindowLifecycle.moc"
