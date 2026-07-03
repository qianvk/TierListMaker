#pragma once

#include <QApplication>
#include <memory>

namespace tlm {

class AppSettings;
class AssetManager;
class LanguageManager;
class Logger;
class ProjectRepository;
class RecentProjectsStore;
class ThemeManager;
class ThumbnailCache;
class AppUpdater;

/** QApplication subclass that owns app-wide services and startup policy. */
class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    int run();

private:
    void configureApplication();
    void configureFont();
    void scheduleAutoUpdateCheck();

    std::unique_ptr<Logger> m_logger;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<LanguageManager> m_languageManager;
    std::unique_ptr<ThemeManager> m_themeManager;
    std::unique_ptr<ProjectRepository> m_repository;
    std::unique_ptr<RecentProjectsStore> m_recentProjects;
    std::unique_ptr<AssetManager> m_assetManager;
    std::unique_ptr<ThumbnailCache> m_thumbnailCache;
    std::unique_ptr<AppUpdater> m_updater;
};

} // namespace tlm
