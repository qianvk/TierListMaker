#include "window/MainWindow.h"

#include "window/AppTitleBar.h"
#include "window/RootWidget.h"

#include <QCloseEvent>

namespace tlm {

MainWindow::MainWindow(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                       AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
                       LanguageManager* languageManager, AppUpdater* updater, QWidget* parent)
    : vkframeless::FramelessWindow(parent) {
    vkframeless::FramelessWindowOptions opts = options();
    opts.backgroundColor = QColor(QStringLiteral("#f5f6f8"));
    opts.backgroundOpacity = 0.98;
    opts.captionButtonIconColor = QColor(QStringLiteral("#30343a"));
    opts.systemButtonVisibility = vkframeless::SystemButtonVisibility::Always;
    opts.paintedCornerRadius = 12;
    setOptions(opts);

    setWindowTitle(QStringLiteral("TierListMaker"));
    resize(1180, 780);
    setMinimumSize(920, 620);

    m_root = new RootWidget(repository, recentProjects, assetManager, thumbnailCache, settings,
                            languageManager, updater, this);
    setFramelessContentWidget(m_root);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    setSystemButtonTitleBar(nullptr);
#else
    setSystemButtonTitleBar(m_root->titleBar());
#endif
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_root && !m_root->confirmClose()) {
        event->ignore();
        return;
    }
    vkframeless::FramelessWindow::closeEvent(event);
}

} // namespace tlm
