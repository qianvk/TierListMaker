#pragma once

#include "navigation/SidebarModel.h"

#include <vkframeless/FramelessWindow.h>

#include <QWidget>

class QFrame;
class QResizeEvent;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QVariantAnimation;

namespace vkframeless {
class WindowTitleBar;
}

namespace tlm {

class AppSettings;
class AppTitleBar;
class AssetManager;
class EditPage;
class LanguageManager;
class PreviewOverlay;
class ProjectRepository;
class ProjectsPage;
class RecentProjectsStore;
class SidebarToggleButton;
class SidebarView;
class ThemeManager;
class ThumbnailCache;
class AppUpdater;

/** Root content widget installed into VKFrameless and responsible for page routing. */
class RootWidget : public QWidget {
    Q_OBJECT

public:
    RootWidget(ProjectRepository* repository, RecentProjectsStore* recentProjects,
               AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
               LanguageManager* languageManager, AppUpdater* updater, QWidget* parent = nullptr);

    AppTitleBar* titleBar() const { return m_titleBar; }
    bool confirmClose();

public slots:
    void switchToPage(AppPage page);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildUi(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                 AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
                 LanguageManager* languageManager, AppUpdater* updater);
    void setupShortcuts();
    void updateSelection(AppPage page);
    void updateTitleBarForPage(AppPage page);
    void updatePageMargins(AppPage page);
    QFrame* createSidebar(QWidget* parent);
    QFrame* createContent(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                          AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                          AppSettings* settings, LanguageManager* languageManager, AppUpdater* updater);
    void setSidebarCollapsed(bool collapsed);
    void setSidebarWidth(int width);
    void synchronizeInitialLayout();
    void syncSidebarPresentation(int sidebarWidth);
    void layoutSidebarSurface();
    void layoutTitleBars();
    void layoutSidebarToggleButton();
    void layoutPreferenceBadge();
    void setUpdateBadgeVisible(bool visible);
    int minimumSidebarToggleX() const;
    void setTierFocusMode(bool enabled);

    AppTitleBar* m_titleBar{nullptr};
    QSplitter* m_splitter{nullptr};
    QFrame* m_sidebarShell{nullptr};
    QFrame* m_sidebar{nullptr};
    QFrame* m_content{nullptr};
    vkframeless::WindowTitleBar* m_sidebarTitleBar{nullptr};
    QVBoxLayout* m_sidebarLayout{nullptr};
    SidebarModel* m_sidebarModel{nullptr};
    SidebarView* m_sidebarView{nullptr};
    SidebarToggleButton* m_sidebarToggleButton{nullptr};
    QToolButton* m_preferencesButton{nullptr};
    QLabel* m_preferencesBadge{nullptr};
    QVariantAnimation* m_sidebarAnimation{nullptr};
    QStackedWidget* m_pages{nullptr};
    EditPage* m_editPage{nullptr};
    ProjectsPage* m_projectsPage{nullptr};
    QWidget* m_preferencesPage{nullptr};
    bool m_sidebarCollapsed{false};
    bool m_tierFocusMode{false};
    vkframeless::SystemButtonVisibility m_savedSystemButtonVisibility{vkframeless::SystemButtonVisibility::Always};
    int m_currentSidebarWidth{0};
    int m_lastExpandedSidebarWidth{240};
    int m_focusSavedSidebarWidth{240};
    bool m_focusSavedSidebarCollapsed{false};
    bool m_initialLayoutSynced{false};
};

} // namespace tlm
