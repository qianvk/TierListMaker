#include <QDialog>
#include <QOperatingSystemVersion>
#include <QPushButton>
#include <QWidget>
#include <QtTest>

#include <QWKWidgets/widgetwindowagent.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#import <AppKit/AppKit.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

class WindowResizePolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void nativeResizeIsOptIn();
    void transientAgentNeverClaimsOwnerWindow();
};

#if defined(Q_OS_WIN)
namespace {
LRESULT hitTestAt(QWidget& host, const QPoint& clientPosition) {
    const auto hwnd = reinterpret_cast<HWND>(host.winId());
    POINT nativePosition{clientPosition.x(), clientPosition.y()};
    ::ClientToScreen(hwnd, &nativePosition);
    return ::SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(nativePosition.x, nativePosition.y));
}
} // namespace
#endif

void WindowResizePolicyTest::nativeResizeIsOptIn() {
    QWidget host;
    host.resize(480, 320);
    QWK::WidgetWindowAgent agent;
    QVERIFY(!agent.isResizable());
    QVERIFY(agent.setup(&host));
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    auto* nativeView = reinterpret_cast<NSView*>(host.winId());
    QVERIFY(nativeView != nil);
    NSWindow* nativeWindow = nativeView.window;
    QVERIFY(nativeWindow != nil);
    QVERIFY(!(nativeWindow.styleMask & NSWindowStyleMaskResizable));

    agent.setResizable(true);
    QCoreApplication::processEvents();
    QVERIFY(nativeWindow.styleMask & NSWindowStyleMaskResizable);

    agent.setResizable(false);
    QCoreApplication::processEvents();
    QVERIFY(!(nativeWindow.styleMask & NSWindowStyleMaskResizable));
#elif defined(Q_OS_WIN)
    const auto hwnd = reinterpret_cast<HWND>(host.winId());
    const auto style = [hwnd]() { return ::GetWindowLongPtrW(hwnd, GWL_STYLE); };
    QVERIFY(!(style() & (WS_THICKFRAME | WS_MAXIMIZEBOX)));

    QVERIFY(agent.installSystemButtons());
    auto* closeButton = host.findChild<QPushButton*>(QStringLiteral("qwkWindowsCloseButton"));
    QVERIFY(closeButton);
    const qreal expectedRadius =
        QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11 ? 8.0 : 0.0;
    QCOMPARE(closeButton->property("_qwk_effective_top_right_corner_radius").toReal(),
             expectedRadius);

    agent.setResizable(true);
    QCoreApplication::processEvents();
    QVERIFY(style() & WS_THICKFRAME);

    agent.setResizable(false);
    QCoreApplication::processEvents();
    QVERIFY(!(style() & (WS_THICKFRAME | WS_MAXIMIZEBOX)));
#else
    agent.setResizable(true);
    QVERIFY(agent.isResizable());
    agent.setResizable(false);
    QVERIFY(!agent.isResizable());
#endif
}

void WindowResizePolicyTest::transientAgentNeverClaimsOwnerWindow() {
#if defined(Q_OS_WIN)
    QWidget host;
    host.resize(640, 420);
    QWidget hostTitleBar(&host);
    hostTitleBar.setGeometry(0, 0, host.width(), 44);

    QWK::WidgetWindowAgent hostAgent;
    hostAgent.setResizable(true);
    QVERIFY(hostAgent.setup(&host));
    QVERIFY(hostAgent.addTitleBar(&hostTitleBar));
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QCOMPARE(hitTestAt(host, QPoint(240, 22)), static_cast<LRESULT>(HTCAPTION));

    QDialog dialog(&host, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                              Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.resize(360, 220);
    QWidget dialogTitleBar(&dialog);
    dialogTitleBar.setGeometry(0, 0, dialog.width(), 44);

    QWK::WidgetWindowAgent dialogAgent;
    QVERIFY(!dialog.internalWinId());
    QVERIFY(dialogAgent.setup(&dialog));
    QVERIFY(dialogAgent.addTitleBar(&dialogTitleBar));

    // Agent setup must not hook the already-native owner while the dialog is still alien.
    QVERIFY(!dialog.internalWinId());
    QCOMPARE(hitTestAt(host, QPoint(240, 22)), static_cast<LRESULT>(HTCAPTION));

    dialog.open();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(dialog.internalWinId());
    dialog.reject();
    QTRY_VERIFY(!dialog.isVisible());

    // Destroying or hiding the transient context must leave the owner's native hit testing intact.
    QCOMPARE(hitTestAt(host, QPoint(240, 22)), static_cast<LRESULT>(HTCAPTION));
#else
    QSKIP("The native owner-window regression is specific to the Windows HWND backend.");
#endif
}

QTEST_MAIN(WindowResizePolicyTest)

#include "tst_WindowResizePolicy.moc"
