#pragma once

#include <vkframeless/FramelessWindow.h>

namespace tlm {

class AssetManager;
class AppSettings;
class LanguageManager;
class ProjectRepository;
class RecentProjectsStore;
class RootWidget;
class ThemeManager;
class ThumbnailCache;
class AppUpdater;

/** Top-level VKFrameless application window with native platform behavior preserved. */
class MainWindow : public vkframeless::FramelessWindow {
    Q_OBJECT

public:
    MainWindow(ProjectRepository* repository, RecentProjectsStore* recentProjects,
               AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
               LanguageManager* languageManager, AppUpdater* updater, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    RootWidget* m_root{nullptr};
};

} // namespace tlm
