#include <QPushButton>
#include <QWidget>
#include <QtTest>

#include <QWKWidgets/widgetwindowagent.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#import <AppKit/AppKit.h>
#elif defined(Q_OS_WIN)
#include <QWKCore/qwindowkit_windows.h>
#include <qt_windows.h>
#endif

class WindowResizePolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void nativeResizeIsOptIn();
};

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
    const qreal expectedRadius = QWK::Private::IsWindows11OrGreater_Real() ? 8.0 : 0.0;
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

QTEST_MAIN(WindowResizePolicyTest)

#include "tst_WindowResizePolicy.moc"
