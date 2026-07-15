#include "window/RootWidget.h"

#include "logging/Logger.h"
#include "logging/UiPerformanceMonitor.h"
#include "navigation/SidebarView.h"
#include "pages/EditPage.h"
#include "pages/PreferencesPage.h"
#include "pages/ProjectsPage.h"
#include "preview/PreviewOverlay.h"
#include "theme/Theme.h"
#include "update/AppUpdater.h"
#include "window/AppDialog.h"
#include "window/AppTitleBar.h"
#include "window/SidebarToggleButton.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEasingCurve>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWindow>

#include <QWKWidgets/widgetwindowagent.h>
#include <vkui/core/VkIcon.h>

#include <algorithm>

namespace tlm {

namespace {
constexpr int kSidebarInitialWidth = 236;
constexpr int kSidebarMinimumExpandedWidth = 208;
constexpr int kSidebarMaximumWidth = 360;
constexpr int kSplitterHandleWidth = 0;
constexpr int kTitleBarHeight = 54;
constexpr int kSidebarToggleInset = 14;
[[maybe_unused]] constexpr int kTitleBarControlGap = 10;

class StatusBadge final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(activeThemeTokens().destructive);
        painter.drawEllipse(rect());
    }
};

Qt::KeyboardModifier physicalControlModifier() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt maps the physical macOS Control key to MetaModifier; ControlModifier is Command.
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

bool sameProjectPath(const QString& left, const QString& right) {
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(left).absoluteFilePath())
               .compare(QDir::cleanPath(QFileInfo(right).absoluteFilePath()), pathCase) == 0;
}

QToolButton* makeTitleBarIconButton(const QString& tooltip, vkui::VkSymbol symbol,
                                    QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setToolTip(tooltip);
    button->setIcon(vkui::icon(symbol));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setCursor(Qt::ArrowCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(34, 34);
    button->setIconSize(QSize(20, 20));
    button->setAutoRaise(true);
    return button;
}
} // namespace

RootWidget::RootWidget(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                       AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                       AppSettings* settings, LanguageManager* languageManager, AppUpdater* updater,
                       QWidget* parent)
    : QWidget(parent) {
    buildUi(repository, recentProjects, assetManager, thumbnailCache, settings, languageManager,
            updater);
    setupShortcuts();
}

void RootWidget::installWindowAgent(QWK::WidgetWindowAgent* agent) {
    m_windowAgent = agent;
    if (!m_windowAgent || !m_sidebarTitleBar || !m_titleBar) {
        Logger::error(QStringLiteral("ui.window.agent.register rejected missing-widget=1"));
        return;
    }

    m_windowAgent->addTitleBar(m_sidebarTitleBar);
    m_windowAgent->addTitleBar(m_titleBar);

    if (m_previewOverlay) {
        // The preview is a window-level input surface. While visible it must supersede every
        // registered title bar so Windows routes pointer input through the client area.
        m_windowAgent->setHitTestVisible(m_sidebarTitleBar, m_previewOverlay, true);
        m_windowAgent->setHitTestVisible(m_titleBar, m_previewOverlay, true);
    }

    const auto registerInteractiveChildren = [this](QWidget* titleBar) {
        const auto buttons = titleBar->findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons) {
            m_windowAgent->setHitTestVisible(titleBar, button, true);
        }
        const auto edits = titleBar->findChildren<QLineEdit*>();
        for (QLineEdit* edit : edits) {
            m_windowAgent->setHitTestVisible(titleBar, edit, true);
        }
    };
    registerInteractiveChildren(m_sidebarTitleBar);
    for (QWidget* control : m_titleBar->interactiveWidgets()) {
        if (control) {
            m_windowAgent->setHitTestVisible(m_titleBar, control, true);
        }
    }

    if (m_sidebarToggleButton) {
        m_windowAgent->setHitTestVisible(m_sidebarTitleBar, m_sidebarToggleButton, true);
        m_windowAgent->setHitTestVisible(m_titleBar, m_sidebarToggleButton, true);
    }
    if (m_newProjectButton) {
        m_windowAgent->setHitTestVisible(m_sidebarTitleBar, m_newProjectButton, true);
        m_windowAgent->setHitTestVisible(m_titleBar, m_newProjectButton, true);
    }
    if (m_splitter && m_splitter->handle(1)) {
        m_windowAgent->setHitTestVisible(m_sidebarTitleBar, m_splitter->handle(1), true);
        m_windowAgent->setHitTestVisible(m_titleBar, m_splitter->handle(1), true);
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    m_windowAgent->setSystemButtonAreaGeometry(QRect(12, 11, 72, 32));
#endif
    setTitleEditorHitTestVisible(m_currentPage == AppPage::Edit && hasActiveProject());
}

bool RootWidget::confirmClose() {
    return !hasActiveProject() || m_editPage->confirmSaveIfDirty();
}

void RootWidget::switchToPage(AppPage page) {
    if (page == AppPage::Preferences) {
        showPreferencesDialog();
        updateSelection(m_currentPage);
        return;
    }
    if (page == AppPage::Edit && !hasActiveProject()) {
        updateSelection(m_currentPage);
        return;
    }

    const int index = stackIndexForPage(page);
    if (index >= 0 && index < m_pages->count()) {
        m_pages->setCurrentIndex(index);
        m_currentPage = page;
        updateSelection(page);
        updatePageMargins(page);
        updateTitleBarForPage(page);
        updateUnsavedIndicators();
    }
}

void RootWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_sidebarShell && m_sidebarShell->isVisible()) {
        m_currentSidebarWidth = m_sidebarShell->width();
        syncSidebarPresentation(m_currentSidebarWidth);
        layoutSidebarSurface();
    }
    layoutTitleBars();
    layoutSidebarToggleButton();
    layoutPreferenceBadge();
    if (m_previewOverlay) {
        m_previewOverlay->setGeometry(rect());
        if (m_previewOverlay->isOpen()) {
            m_previewOverlay->raise();
        }
    }
}

void RootWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_initialLayoutSynced) {
        synchronizeInitialLayout();
        return;
    }

    m_initialLayoutSynced = true;
    QTimer::singleShot(0, this, [this]() { synchronizeInitialLayout(); });
}

void RootWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
        if (m_sidebarTitleBar) {
            m_sidebarTitleBar->update();
        }
        update();
    }
}

void RootWidget::buildUi(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                         AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                         AppSettings* settings, LanguageManager* languageManager,
                         AppUpdater* updater) {
    m_settings = settings;
    m_languageManager = languageManager;
    m_updater = updater;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("MainSplitter"));
    m_splitter->setHandleWidth(kSplitterHandleWidth);
    m_splitter->setOpaqueResize(true);
    m_splitter->setStyleSheet(
        QStringLiteral("QSplitter#MainSplitter::handle{background:transparent;border:none;margin:0;"
                       "padding:0;width:0px;}"
                       "QSplitter#MainSplitter::handle:hover{background:transparent;}"));
    root->addWidget(m_splitter, 1);

    m_sidebarShell = new QFrame(m_splitter);
    m_sidebarShell->setObjectName(QStringLiteral("SidebarShell"));
    m_sidebarShell->setMinimumWidth(0);
    m_sidebarShell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_sidebarShell->setFrameShape(QFrame::NoFrame);
    m_sidebarShell->setBackgroundRole(QPalette::Window);
    m_sidebarShell->setAutoFillBackground(true);

    m_sidebar = createSidebar(m_sidebarShell);
    m_content = createContent(repository, recentProjects, assetManager, thumbnailCache, settings);

    m_previewOverlay = new PreviewOverlay(this);
    m_previewOverlay->setGeometry(rect());
    m_previewOverlay->setBackgroundMode(settings ? settings->previewBackgroundMode()
                                                 : PreviewBackgroundMode::None);
    connect(m_editPage, &EditPage::imagePreviewRequested, m_previewOverlay,
            &PreviewOverlay::openPreview);
    connect(m_editPage, &EditPage::imagePreviewCloseRequested, m_previewOverlay,
            &PreviewOverlay::closePreview);
    if (settings) {
        connect(settings, &AppSettings::previewBackgroundModeChanged, m_previewOverlay,
                &PreviewOverlay::setBackgroundMode);
    }
    connect(m_previewOverlay, &PreviewOverlay::opened, this, [this]() {
        if (m_windowAgent && !m_previewSystemButtonStateCaptured) {
            m_previewSavedSystemButtonVisibility = m_windowAgent->systemButtonVisibility();
            m_previewSystemButtonStateCaptured = true;
            m_windowAgent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysHidden);
        }
        Logger::info(QStringLiteral("ui.preview.input.barrier enabled=1 scope=window"));
    });
    connect(m_previewOverlay, &PreviewOverlay::closed, this, [this]() {
        if (m_windowAgent && m_previewSystemButtonStateCaptured) {
            m_windowAgent->setSystemButtonVisibility(m_previewSavedSystemButtonVisibility);
        }
        m_previewSystemButtonStateCaptured = false;
        Logger::info(QStringLiteral("ui.preview.input.barrier enabled=0 scope=window"));
    });

    m_splitter->addWidget(m_sidebarShell);
    m_splitter->addWidget(m_content);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    connect(m_sidebarView, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.flags().testFlag(Qt::ItemIsEnabled)) {
            updateSelection(m_currentPage);
            return;
        }
        switchToPage(m_sidebarModel->pageForIndex(index));
    });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int pos, int index) {
        if (m_tierFocusMode) {
            setSidebarWidth(0);
            return;
        }
        m_currentSidebarWidth = m_sidebarShell ? m_sidebarShell->width() : 0;

        const bool widthAnimationRunning =
            m_sidebarAnimation && m_sidebarAnimation->state() == QAbstractAnimation::Running;
        if (!widthAnimationRunning && m_currentSidebarWidth > 0 &&
            m_currentSidebarWidth < kSidebarMinimumExpandedWidth) {
            // Keep the manually resized navigation usable. Programmatic collapse still animates
            // through intermediate widths, while drag resizing has a stable expanded minimum.
            const QSignalBlocker blocker(m_splitter);
            setSidebarWidth(kSidebarMinimumExpandedWidth);
            m_currentSidebarWidth = m_sidebarShell ? m_sidebarShell->width() : 0;
        }

        if (m_currentSidebarWidth > 0) {
            m_sidebarCollapsed = false;
            if (m_currentSidebarWidth >= kSidebarMinimumExpandedWidth) {
                m_lastExpandedSidebarWidth = m_currentSidebarWidth;
            }
            if (m_sidebarToggleButton) {
                m_sidebarToggleButton->setToolTip(tr("Collapse sidebar"));
            }
        } else {
            m_sidebarCollapsed = true;
            if (m_sidebarToggleButton) {
                m_sidebarToggleButton->setToolTip(tr("Show sidebar"));
            }
        }
        syncSidebarPresentation(m_currentSidebarWidth);
        layoutSidebarSurface();
        layoutTitleBars();
        layoutSidebarToggleButton();
        Logger::debug(QStringLiteral("ui.main.splitter.moved pos=%1 index=%2 sidebarWidth=%3")
                          .arg(pos)
                          .arg(index)
                          .arg(m_currentSidebarWidth));
    });

    m_newProjectButton = makeTitleBarIconButton(tr("New"), vkui::VkSymbol::Plus, this);
    m_newProjectButton->setObjectName(QStringLiteral("NewProjectButton"));
    connect(m_newProjectButton, &QToolButton::clicked, this, [this]() {
        if (m_editPage && m_editPage->newProject()) {
            setActiveProjectAvailable(true);
            switchToPage(AppPage::Edit);
        }
    });

    m_sidebarToggleButton = new SidebarToggleButton(this);
    m_sidebarToggleButton->setObjectName(QStringLiteral("SidebarToggleButton"));
    m_sidebarToggleButton->setToolTip(tr("Collapse sidebar"));
    connect(m_sidebarToggleButton, &QAbstractButton::clicked, this,
            [this]() { setSidebarCollapsed(!m_sidebarCollapsed); });

    if (updater) {
        connect(updater, &AppUpdater::updateNotificationChanged, this,
                &RootWidget::setUpdateBadgeVisible);
        setUpdateBadgeVisible(updater->hasUpdateAvailable());
    }

    m_sidebarAnimation = new QVariantAnimation(this);
    m_sidebarAnimation->setDuration(260);
    m_sidebarAnimation->setEasingCurve(QEasingCurve::OutQuint);
    connect(m_sidebarAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { setSidebarWidth(qRound(value.toReal())); });

    connect(m_projectsPage, &ProjectsPage::openProjectRequested, this, [this](const QString& path) {
        if (sameProjectPath(m_editPage->currentProjectPath(), path)) {
            setActiveProjectAvailable(true);
            switchToPage(AppPage::Edit);
            return;
        }
        if (m_editPage->confirmSaveIfDirty() && m_editPage->openProject(path)) {
            setActiveProjectAvailable(true);
            switchToPage(AppPage::Edit);
        }
    });
    connect(m_projectsPage, &ProjectsPage::projectDeleted, this, [this](const QString& path) {
        if (!m_editPage || !sameProjectPath(m_editPage->currentProjectPath(), path)) {
            return;
        }
        m_editPage->clearProject();
        setActiveProjectAvailable(false);
    });

    connect(m_editPage, &EditPage::titleChanged, this, [this](const QString& title) {
        if (m_pages && m_pages->currentWidget() == m_editPage && !m_tierFocusMode &&
            hasActiveProject()) {
            m_titleBar->setTitleEditable(true);
            m_titleBar->setDocumentTitle(title);
        }
    });
    connect(m_editPage, &EditPage::dirtyChanged, this, &RootWidget::updateUnsavedIndicators);
    connect(m_editPage, &EditPage::projectSaved, this, &RootWidget::updateUnsavedIndicators);
    connect(m_titleBar, &AppTitleBar::templatesRequested, m_editPage, &EditPage::showTemplateMenu);
    connect(m_titleBar, &AppTitleBar::backgroundRequested, m_editPage,
            &EditPage::configureBackground);
    connect(m_titleBar, &AppTitleBar::galleryRequested, m_editPage, &EditPage::toggleGallery);
    connect(m_titleBar, &AppTitleBar::resetRowsRequested, m_editPage, &EditPage::resetRows);
    connect(m_titleBar, &AppTitleBar::projectTitleEdited, m_editPage, &EditPage::renameProject);
    connect(m_titleBar, &AppTitleBar::tierFocusModeRequested, this,
            [this]() { setTierFocusMode(!m_tierFocusMode); });
    connect(m_editPage, &EditPage::galleryMissionControlRequested, this, [this]() {
        if (m_editPage) {
            m_editPage->toggleGalleryMissionControlMode();
        }
    });
    connect(m_editPage, &EditPage::resetRowsAvailableChanged, m_titleBar,
            &AppTitleBar::setResetRowsActionEnabled);
    m_titleBar->setResetRowsActionEnabled(false);
    connect(languageManager, &LanguageManager::languageChanged, m_sidebarModel,
            &SidebarModel::retranslate);
    connect(languageManager, &LanguageManager::languageChanged, this, [this]() {
        if (m_sidebarView) {
            const int preferred = std::clamp(m_sidebarView->preferredWidth() + 22,
                                             kSidebarMinimumExpandedWidth, kSidebarMaximumWidth);
            m_lastExpandedSidebarWidth = std::max(m_lastExpandedSidebarWidth, preferred);
            if (!m_sidebarCollapsed && m_currentSidebarWidth > 0) {
                setSidebarWidth(std::max(m_currentSidebarWidth, preferred));
            }
        }
        if (m_titleBar) {
            m_titleBar->retranslateUi();
        }
        if (m_projectsPage) {
            m_projectsPage->retranslateUi();
        }
        if (m_pages) {
            updateTitleBarForPage(m_currentPage);
        }
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->setToolTip(m_sidebarCollapsed ? tr("Show sidebar")
                                                                 : tr("Collapse sidebar"));
        }
        if (m_newProjectButton) {
            m_newProjectButton->setToolTip(tr("New"));
        }
        if (m_preferencesButton) {
            m_preferencesButton->setToolTip(tr("Preferences"));
        }
    });

    m_sidebarModel->setPageEnabled(AppPage::Edit, false);
    m_sidebarModel->setPageDirty(AppPage::Edit, false);
    const int preferredSidebarWidth =
        std::clamp(m_sidebarView ? m_sidebarView->preferredWidth() + 22 : kSidebarInitialWidth,
                   kSidebarMinimumExpandedWidth, kSidebarMaximumWidth);
    m_lastExpandedSidebarWidth = preferredSidebarWidth;
    setSidebarWidth(preferredSidebarWidth);
    switchToPage(AppPage::Projects);
    layoutTitleBars();
    layoutSidebarToggleButton();
    updateTitleBarLeadingReservation();
    QTimer::singleShot(0, this, [this]() { synchronizeInitialLayout(); });
}

QFrame* RootWidget::createSidebar(QWidget* parent) {
    auto* sidebar = new QFrame(parent);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setBackgroundRole(QPalette::Window);
    sidebar->setAutoFillBackground(true);

    m_sidebarLayout = new QVBoxLayout(sidebar);
    m_sidebarLayout->setContentsMargins(12, kTitleBarHeight + 12, 10, 14);
    m_sidebarLayout->setSpacing(12);

    m_sidebarTitleBar = new QWidget(sidebar);
    m_sidebarTitleBar->setObjectName(QStringLiteral("SidebarTitleBar"));
    m_sidebarTitleBar->setFixedHeight(kTitleBarHeight);
    m_sidebarTitleBar->setAttribute(Qt::WA_StyledBackground, false);

    m_sidebarModel = new SidebarModel(this);
    m_sidebarView = new SidebarView(sidebar);
    m_sidebarView->setModel(m_sidebarModel);
    m_sidebarLayout->addWidget(m_sidebarView);
    m_sidebarLayout->addStretch();

    m_preferencesButton = new QToolButton(sidebar);
    m_preferencesButton->setObjectName(QStringLiteral("PreferencesButton"));
    m_preferencesButton->setIcon(vkui::icon(vkui::VkSymbol::Settings));
    m_preferencesButton->setToolTip(tr("Preferences"));
    m_preferencesButton->setCursor(Qt::ArrowCursor);
    m_preferencesButton->setFixedSize(34, 34);
    m_preferencesButton->setIconSize(QSize(18, 18));
    m_preferencesButton->setAutoRaise(true);
    connect(m_preferencesButton, &QToolButton::clicked, this,
            [this]() { switchToPage(AppPage::Preferences); });
    m_sidebarLayout->addWidget(m_preferencesButton, 0, Qt::AlignLeft);

    m_preferencesBadge = new StatusBadge(m_preferencesButton);
    m_preferencesBadge->setObjectName(QStringLiteral("PreferencesUpdateBadge"));
    m_preferencesBadge->setFixedSize(8, 8);
    m_preferencesBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_preferencesBadge->hide();
    layoutPreferenceBadge();
    return sidebar;
}

QFrame* RootWidget::createContent(ProjectRepository* repository,
                                  RecentProjectsStore* recentProjects, AssetManager* assetManager,
                                  ThumbnailCache* thumbnailCache, AppSettings* settings) {
    auto* content = new QFrame(m_splitter);
    content->setObjectName(QStringLiteral("Content"));
    content->setMinimumWidth(620);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setBackgroundRole(QPalette::Base);
    content->setAutoFillBackground(true);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_pages = new QStackedWidget(content);
    m_pages->setObjectName(QStringLiteral("Pages"));
    m_pages->setContentsMargins(0, 0, 0, 0);
    m_editPage =
        new EditPage(repository, recentProjects, assetManager, thumbnailCache, settings, content);
    m_projectsPage = new ProjectsPage(repository, recentProjects, settings, content);
    m_pages->addWidget(m_editPage);
    m_pages->addWidget(m_projectsPage);
    contentLayout->addWidget(m_pages, 1);

    // The chrome is intentionally a transparent overlay. EditPage reserves exactly one title-bar
    // height for the board, while its lightweight shadow can remain visible below this widget.
    m_titleBar = new AppTitleBar(content);
    m_titleBar->setGeometry(0, 0, content->width(), kTitleBarHeight);
    m_titleBar->raiseChrome();
    return content;
}

void RootWidget::setSidebarCollapsed(bool collapsed) {
    if (m_tierFocusMode) {
        return;
    }
    if (m_sidebarCollapsed == collapsed || !m_sidebarAnimation) {
        return;
    }

    m_sidebarCollapsed = collapsed;
    m_sidebarToggleButton->setToolTip(collapsed ? tr("Show sidebar") : tr("Collapse sidebar"));
    layoutSidebarToggleButton();

    if (m_sidebarShell) {
        m_sidebarShell->show();
    }
    if (m_sidebar) {
        m_sidebar->show();
    }

    const int startWidth = m_currentSidebarWidth;
    const int endWidth =
        collapsed ? 0 : std::max(kSidebarMinimumExpandedWidth, m_lastExpandedSidebarWidth);
    m_sidebarAnimation->stop();
    m_sidebarAnimation->setStartValue(static_cast<double>(startWidth));
    m_sidebarAnimation->setEndValue(static_cast<double>(endWidth));
    m_sidebarAnimation->start();
    m_sidebarAnimation->setCurrentTime(1);
    Logger::info(QStringLiteral("ui.sidebar.toggle collapsed=%1 startWidth=%2 endWidth=%3")
                     .arg(collapsed)
                     .arg(startWidth)
                     .arg(endWidth));
}

void RootWidget::setSidebarWidth(int width) {
    if (!m_splitter || !m_sidebarShell || !m_sidebar) {
        return;
    }

    const int totalWidth =
        std::max(width + (m_content ? m_content->minimumWidth() : 620), m_splitter->width());
    const int maxSidebarWidth =
        std::clamp(totalWidth - (m_content ? m_content->minimumWidth() : 620),
                   kSidebarMinimumExpandedWidth, kSidebarMaximumWidth);
    const int sidebarWidth = std::clamp(width, 0, maxSidebarWidth);

    m_sidebarShell->show();
    m_sidebar->show();
    m_currentSidebarWidth = sidebarWidth;
    m_sidebarShell->setMaximumWidth(sidebarWidth == 0 ? 0 : kSidebarMaximumWidth);
    m_splitter->setSizes({sidebarWidth, std::max(1, totalWidth - sidebarWidth)});

    syncSidebarPresentation(sidebarWidth);
    layoutSidebarSurface();
    layoutTitleBars();
    layoutSidebarToggleButton();
    layoutPreferenceBadge();
}

void RootWidget::synchronizeInitialLayout() {
    if (!m_splitter || m_tierFocusMode) {
        layoutTitleBars();
        layoutSidebarToggleButton();
        return;
    }

    const int targetWidth =
        m_sidebarCollapsed ? 0 : std::max(kSidebarMinimumExpandedWidth, m_lastExpandedSidebarWidth);
    setSidebarWidth(targetWidth);
    if (m_pages) {
        updatePageMargins(m_currentPage);
        updateTitleBarForPage(m_currentPage);
    }
    syncSidebarPresentation(m_currentSidebarWidth);
    layoutSidebarSurface();
    layoutTitleBars();
    layoutSidebarToggleButton();
}

void RootWidget::setTierFocusMode(bool enabled) {
    if (m_tierFocusMode == enabled) {
        return;
    }

    if (m_sidebarAnimation) {
        m_sidebarAnimation->stop();
    }

    m_tierFocusMode = enabled;
    if (enabled) {
        qApp->installEventFilter(this);
    } else {
        qApp->removeEventFilter(this);
        m_focusMoveWindow = nullptr;
    }
    UiPerformanceMonitor::setTierFocusMode(enabled);
    Logger::info(QStringLiteral("ui.tier.focus.mode enabled=%1").arg(enabled));

    if (enabled) {
        if (m_windowAgent) {
            m_savedSystemButtonVisibility = m_windowAgent->systemButtonVisibility();
            m_windowAgent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysHidden);
        }
        m_focusSavedSidebarCollapsed = m_sidebarCollapsed || m_currentSidebarWidth <= 0;
        m_focusSavedSidebarWidth =
            m_focusSavedSidebarCollapsed
                ? std::max(kSidebarMinimumExpandedWidth, m_lastExpandedSidebarWidth)
                : std::max(kSidebarMinimumExpandedWidth, m_currentSidebarWidth);
        if (m_pages) {
            m_pages->setContentsMargins(0, 0, 0, 0);
        }
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->hide();
        }
        if (m_newProjectButton) {
            m_newProjectButton->hide();
        }
        if (m_titleBar) {
            m_titleBar->setTierFocusMode(true);
            m_titleBar->hide();
        }
        if (m_editPage) {
            m_editPage->setTierFocusMode(true);
        }
        setSidebarWidth(0);
    } else {
        if (m_windowAgent) {
            m_windowAgent->setSystemButtonVisibility(m_savedSystemButtonVisibility);
        }
        if (m_pages) {
            updatePageMargins(m_currentPage);
        }
        if (m_titleBar) {
            m_titleBar->show();
            m_titleBar->setTierFocusMode(false);
        }
        if (m_editPage) {
            m_editPage->setTierFocusMode(false);
        }
        m_sidebarCollapsed = m_focusSavedSidebarCollapsed;
        setSidebarWidth(m_sidebarCollapsed
                            ? 0
                            : std::clamp(m_focusSavedSidebarWidth, kSidebarMinimumExpandedWidth,
                                         kSidebarMaximumWidth));
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->show();
            m_sidebarToggleButton->setToolTip(m_sidebarCollapsed ? tr("Show sidebar")
                                                                 : tr("Collapse sidebar"));
        }
        if (m_newProjectButton) {
            m_newProjectButton->show();
        }
        if (m_pages) {
            updateTitleBarForPage(m_currentPage);
        }
    }

    layoutTitleBars();
    layoutSidebarToggleButton();
}

bool RootWidget::eventFilter(QObject* watched, QEvent* event) {
    if (!m_tierFocusMode || !event) {
        return QWidget::eventFilter(watched, event);
    }

    if (m_focusMoveWindow && event->type() == QEvent::MouseMove) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            m_focusMoveWindow->move(mouseEvent->globalPosition().toPoint() - m_focusMoveOffset);
            mouseEvent->accept();
            return true;
        }
        m_focusMoveWindow = nullptr;
    } else if (m_focusMoveWindow && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_focusMoveWindow = nullptr;
            mouseEvent->accept();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        auto* target = qobject_cast<QWidget*>(watched);
        if (mouseEvent->button() == Qt::LeftButton &&
            mouseEvent->modifiers().testFlag(Qt::AltModifier) && target &&
            target->window() == window()) {
            // RootWidget is the central widget, so its own windowHandle() is normally null.
            // QWindow::startSystemMove() must be invoked on the top-level native window.
            QWidget* topLevel = target->window();
            QWindow* nativeWindow = topLevel ? topLevel->windowHandle() : nullptr;
            if (nativeWindow && nativeWindow->startSystemMove()) {
                Logger::debug(QStringLiteral("ui.tier.focus.window.move"));
                mouseEvent->accept();
                return true;
            }
            // Some compositors reject native system move for custom full-content windows. Keep a
            // small cross-platform fallback so Option/Alt-drag remains available everywhere.
            if (topLevel) {
                m_focusMoveWindow = topLevel;
                m_focusMoveOffset =
                    mouseEvent->globalPosition().toPoint() - topLevel->frameGeometry().topLeft();
                Logger::warn(QStringLiteral("ui.tier.focus.window.move fallback=manual"));
                mouseEvent->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RootWidget::syncSidebarPresentation(int sidebarWidth) {
    if (!m_sidebarView) {
        return;
    }
    m_sidebarView->setAvailableWidth(qMax(0, sidebarWidth - 22));
    m_sidebarView->setVisible(sidebarWidth >= 72);
}

void RootWidget::layoutSidebarSurface() {
    if (!m_sidebarShell || !m_sidebar) {
        return;
    }

    // The shell owns the animated width. The real sidebar remains expanded and is clipped by the
    // shell, matching QWindowKit's multi-titlebar demo and avoiding QSplitter minimum-size jumps.
    const int sidebarWidth = std::max(m_sidebarShell->width(), m_lastExpandedSidebarWidth);
    m_sidebar->setGeometry(0, 0, sidebarWidth, m_sidebarShell->height());
}

void RootWidget::layoutTitleBars() {
    if (m_sidebarTitleBar) {
        m_sidebarTitleBar->setGeometry(0, 0, std::max(0, m_currentSidebarWidth), kTitleBarHeight);
        m_sidebarTitleBar->raise();
    }
    if (m_titleBar) {
        const int contentWidth = m_content ? std::max(0, m_content->width()) : width();
        if (m_tierFocusMode) {
            const int revealWidth =
                std::min(contentWidth, m_titleBar->focusRevealSizeHint().width());
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
            m_titleBar->setGeometry(std::max(0, contentWidth - revealWidth), 0, revealWidth,
                                    kTitleBarHeight);
#else
            m_titleBar->setGeometry(0, 0, revealWidth, kTitleBarHeight);
#endif
        } else {
            m_titleBar->setGeometry(0, 0, contentWidth, kTitleBarHeight);
        }
        m_titleBar->raiseChrome();
    }
    if (m_newProjectButton) {
        m_newProjectButton->raise();
    }
    if (m_sidebarToggleButton) {
        m_sidebarToggleButton->raise();
    }
    updateTitleBarLeadingReservation();
    if (m_previewOverlay && m_previewOverlay->isOpen()) {
        m_previewOverlay->raise();
    }
}

void RootWidget::layoutSidebarToggleButton() {
    if (!m_sidebarToggleButton || !m_newProjectButton) {
        return;
    }

    constexpr int kChromeButtonGap = 6;
    const int groupWidth =
        m_newProjectButton->width() + kChromeButtonGap + m_sidebarToggleButton->width();
    const int minimumX = minimumSidebarToggleX();
    const int titleBarWidth = m_sidebarTitleBar ? m_sidebarTitleBar->width() : width();
    const int maximumX = std::max(minimumX, titleBarWidth - groupWidth - kSidebarToggleInset);
    const int naturalX = m_currentSidebarWidth > 0
                             ? m_currentSidebarWidth - groupWidth - kSidebarToggleInset
                             : minimumX;
    const int x = std::clamp(std::max(naturalX, minimumX), minimumX, maximumX);
    const int y = std::max(0, (kTitleBarHeight - m_sidebarToggleButton->height()) / 2);
    m_newProjectButton->move(x, y);
    m_sidebarToggleButton->move(x + m_newProjectButton->width() + kChromeButtonGap, y);
    m_newProjectButton->raise();
    m_sidebarToggleButton->raise();
    updateTitleBarLeadingReservation();
}

void RootWidget::layoutPreferenceBadge() {
    if (!m_preferencesButton || !m_preferencesBadge) {
        return;
    }
    m_preferencesBadge->move(m_preferencesButton->width() - m_preferencesBadge->width() - 4, 5);
    m_preferencesBadge->raise();
}

void RootWidget::setUpdateBadgeVisible(bool visible) {
    if (!m_preferencesBadge) {
        return;
    }
    m_preferencesBadge->setVisible(visible);
    layoutPreferenceBadge();
}

void RootWidget::showPreferencesDialog() {
    if (m_preferencesDialog) {
        m_preferencesDialog->show();
        m_preferencesDialog->raise();
        m_preferencesDialog->activateWindow();
        return;
    }

    auto* dialog = new AppDialog(tr("Preferences"), window());
    dialog->setObjectName(QStringLiteral("PreferencesDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumSize(720, 480);
    dialog->resize(860, 560);

    auto* page = new PreferencesPage(m_settings, m_languageManager, m_updater, dialog);
    dialog->contentLayout()->setContentsMargins(0, 0, 0, 0);
    dialog->contentLayout()->addWidget(page, 1);

    m_preferencesDialog = dialog;
    connect(dialog, &QObject::destroyed, this, [this]() { m_preferencesDialog = nullptr; });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

int RootWidget::stackIndexForPage(AppPage page) const {
    switch (page) {
    case AppPage::Edit:
        return 0;
    case AppPage::Projects:
        return 1;
    case AppPage::Preferences:
        return -1;
    }
    return -1;
}

void RootWidget::updateTitleBarLeadingReservation() {
    if (!m_titleBar) {
        return;
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    m_titleBar->setLeadingReservedWidth(0);
#else
    if (!m_sidebarToggleButton || !m_content || !m_sidebarToggleButton->isVisible() ||
        m_tierFocusMode) {
        m_titleBar->setLeadingReservedWidth(0);
        return;
    }

    const int rightEdge = std::max(m_sidebarToggleButton->geometry().right(),
                                   m_newProjectButton ? m_newProjectButton->geometry().right() : 0);
    const int toggleRightInContent = rightEdge + 1 - m_content->x();
    const int reservedWidth =
        toggleRightInContent > 0 ? toggleRightInContent + kTitleBarControlGap : 0;
    m_titleBar->setLeadingReservedWidth(reservedWidth);
#endif
}

int RootWidget::minimumSidebarToggleX() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (m_windowAgent &&
        m_windowAgent->systemButtonVisibility() != QWK::WindowAgentBase::AlwaysHidden) {
        const QRect area = m_windowAgent->systemButtonAreaGeometry();
        if (area.isValid()) {
            return area.right() + 9;
        }
    }
#endif
    return kSidebarToggleInset;
}

void RootWidget::setupShortcuts() {
    auto addShortcut = [this](const QKeySequence& sequence, const QObject* receiver,
                              const char* slot) {
        auto* shortcut = new QShortcut(sequence, this);
        connect(shortcut, SIGNAL(activated()), receiver, slot);
    };
    auto* newShortcut = new QShortcut(QKeySequence::New, this);
    connect(newShortcut, &QShortcut::activated, this, [this]() {
        if (m_editPage && m_editPage->newProject()) {
            setActiveProjectAvailable(true);
            switchToPage(AppPage::Edit);
        }
    });
    addShortcut(QKeySequence::Open, m_projectsPage, SLOT(openProjectFromDialog()));
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::ApplicationShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this]() {
        if (m_editPage && hasActiveProject() && m_editPage->isDirty()) {
            m_editPage->saveProject();
        }
    });
    auto* missionShortcut =
        new QShortcut(QKeySequence(QKeyCombination(physicalControlModifier(), Qt::Key_I)), this);
    connect(missionShortcut, &QShortcut::activated, m_editPage,
            &EditPage::toggleMissionControlMode);
    auto* galleryMissionShortcut =
        new QShortcut(QKeySequence(QKeyCombination(physicalControlModifier(), Qt::Key_P)), this);
    connect(galleryMissionShortcut, &QShortcut::activated, this, [this]() {
        if (m_editPage) {
            m_editPage->toggleGalleryMissionControlMode();
        }
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), m_editPage, SLOT(exportProjectFromDialog()));

    auto* preferencesShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this);
    connect(preferencesShortcut, &QShortcut::activated, this,
            [this]() { switchToPage(AppPage::Preferences); });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, m_projectsPage, &ProjectsPage::focusSearch);
    auto* previewShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(previewShortcut, &QShortcut::activated, m_editPage, &EditPage::previewSelectedImage);
    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, m_editPage, &EditPage::deleteSelectedImage);
    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        if (m_tierFocusMode) {
            setTierFocusMode(false);
        }
    });
}

void RootWidget::updateSelection(AppPage page) {
    const QModelIndex index = m_sidebarModel->indexForPage(page);
    if (index.isValid()) {
        m_sidebarView->setCurrentIndex(index);
    } else {
        m_sidebarView->clearSelection();
    }
}

void RootWidget::updateTitleBarForPage(AppPage page) {
    if (!m_titleBar || m_tierFocusMode) {
        return;
    }

    switch (page) {
    case AppPage::Edit:
        m_titleBar->setEditorActionsVisible(true);
        m_titleBar->setTitleEditable(hasActiveProject());
        setTitleEditorHitTestVisible(hasActiveProject());
        m_titleBar->setDocumentTitle(m_editPage ? m_editPage->displayTitle() : QString());
        break;
    case AppPage::Projects:
        m_titleBar->setEditorActionsVisible(false);
        m_titleBar->setTitleEditable(false);
        setTitleEditorHitTestVisible(false);
        m_titleBar->setDocumentTitle(tr("Projects"));
        break;
    case AppPage::Preferences:
        m_titleBar->setEditorActionsVisible(false);
        m_titleBar->setTitleEditable(false);
        setTitleEditorHitTestVisible(false);
        m_titleBar->setDocumentTitle(tr("Preferences"));
        break;
    }
    updateUnsavedIndicators();
}

void RootWidget::setTitleEditorHitTestVisible(bool visible) {
    if (!m_windowAgent || !m_titleBar || !m_titleBar->titleEditor()) {
        return;
    }
    m_windowAgent->setHitTestVisible(m_titleBar, m_titleBar->titleEditor(), visible);
}

void RootWidget::updatePageMargins(AppPage page) {
    if (!m_pages) {
        return;
    }
    if (m_tierFocusMode || page == AppPage::Edit) {
        m_pages->setContentsMargins(0, 0, 0, 0);
        return;
    }
    m_pages->setContentsMargins(0, kTitleBarHeight, 0, 0);
}

bool RootWidget::hasActiveProject() const {
    return m_hasActiveProject;
}

void RootWidget::setActiveProjectAvailable(bool available) {
    if (m_hasActiveProject == available) {
        updateUnsavedIndicators();
        return;
    }

    m_hasActiveProject = available;
    if (m_sidebarModel) {
        m_sidebarModel->setPageEnabled(AppPage::Edit, available);
    }
    if (!available && m_currentPage == AppPage::Edit) {
        switchToPage(AppPage::Projects);
    }
    updateTitleBarForPage(m_currentPage);
    updateUnsavedIndicators();
}

void RootWidget::updateUnsavedIndicators() {
    const bool dirty = hasActiveProject() && m_editPage && m_editPage->isDirty();
    if (m_sidebarModel) {
        m_sidebarModel->setPageDirty(AppPage::Edit, dirty);
    }
    if (m_titleBar) {
        m_titleBar->setUnsavedIndicatorVisible(dirty && m_currentPage == AppPage::Edit);
    }
}

} // namespace tlm
