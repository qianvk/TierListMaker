#pragma once

#include "navigation/SidebarModel.h"

#include <QWidget>

#include <QWKCore/windowagentbase.h>

class QFrame;
class QResizeEvent;
class QShowEvent;
class QSplitter;
class QStackedWidget;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QVariantAnimation;

namespace QWK {
class WidgetWindowAgent;
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

/** Root content widget registered with QWindowKit and responsible for page routing. */
class RootWidget : public QWidget {
    Q_OBJECT

public:
    RootWidget(ProjectRepository* repository, RecentProjectsStore* recentProjects,
               AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
               LanguageManager* languageManager, AppUpdater* updater, QWidget* parent = nullptr);

    AppTitleBar* titleBar() const {
        return m_titleBar;
    }
    bool confirmClose();
    void installWindowAgent(QWK::WidgetWindowAgent* agent);

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
                          AppSettings* settings, LanguageManager* languageManager,
                          AppUpdater* updater);
    void setSidebarCollapsed(bool collapsed);
    void setSidebarWidth(int width);
    void synchronizeInitialLayout();
    void syncSidebarPresentation(int sidebarWidth);
    void layoutSidebarSurface();
    void layoutTitleBars();
    void layoutSidebarToggleButton();
    void layoutPreferenceBadge();
    void updateTitleBarLeadingReservation();
    void setUpdateBadgeVisible(bool visible);
    int minimumSidebarToggleX() const;
    void setTierFocusMode(bool enabled);

    AppTitleBar* m_titleBar{nullptr};
    QWK::WidgetWindowAgent* m_windowAgent{nullptr};
    QSplitter* m_splitter{nullptr};
    QFrame* m_sidebarShell{nullptr};
    QFrame* m_sidebar{nullptr};
    QFrame* m_content{nullptr};
    QWidget* m_sidebarTitleBar{nullptr};
    QVBoxLayout* m_sidebarLayout{nullptr};
    SidebarModel* m_sidebarModel{nullptr};
    SidebarView* m_sidebarView{nullptr};
    QToolButton* m_newProjectButton{nullptr};
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
    QWK::WindowAgentBase::SystemButtonVisibility m_savedSystemButtonVisibility{
        QWK::WindowAgentBase::AlwaysVisible};
    int m_currentSidebarWidth{0};
    int m_lastExpandedSidebarWidth{240};
    int m_focusSavedSidebarWidth{240};
    bool m_focusSavedSidebarCollapsed{false};
    bool m_initialLayoutSynced{false};
};

} // namespace tlm
