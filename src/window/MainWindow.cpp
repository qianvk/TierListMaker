#include "window/MainWindow.h"

#include "window/AppTitleBar.h"
#include "window/RootWidget.h"

#include "logging/Logger.h"

#include <QCloseEvent>

#include <QWKWidgets/widgetwindowagent.h>

namespace tlm {

MainWindow::MainWindow(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                       AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                       AppSettings* settings, LanguageManager* languageManager, AppUpdater* updater,
                       QWidget* parent)
    : QMainWindow(parent), m_windowAgent(new QWK::WidgetWindowAgent(this)) {
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    // QWindowKit keeps transient windows fixed by default. The application shell is the explicit
    // resizable host and opts in before the native context is created.
    m_windowAgent->setResizable(true);
    if (!m_windowAgent->setup(this)) {
        Logger::error(QStringLiteral("ui.window.agent.setup failed backend=qwindowkit"));
    } else if (!m_windowAgent->installSystemButtons()) {
        Logger::warn(
            QStringLiteral("ui.window.agent.system-buttons.install failed backend=qwindowkit"));
    }

    setWindowTitle(QStringLiteral("TierListMaker"));
    resize(1180, 780);
    setMinimumSize(920, 620);

    m_root = new RootWidget(repository, recentProjects, assetManager, thumbnailCache, settings,
                            languageManager, updater, this);
    setCentralWidget(m_root);
    m_root->installWindowAgent(m_windowAgent);
    Logger::info(QStringLiteral("ui.window.agent.ready backend=qwindowkit titleBars=2"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_root && !m_root->confirmClose()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

} // namespace tlm
