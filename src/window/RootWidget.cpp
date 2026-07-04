#include "window/RootWidget.h"

#include "logging/Logger.h"
#include "navigation/SidebarView.h"
#include "pages/EditPage.h"
#include "pages/PreferencesPage.h"
#include "pages/ProjectsPage.h"
#include "update/AppUpdater.h"
#include "window/AppTitleBar.h"
#include "window/SidebarToggleButton.h"

#include <vkframeless/FramelessWindow.h>
#include <vkframeless/WindowTitleBar.h>

#include <QAbstractButton>
#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>

namespace tlm {

namespace {
constexpr int kSidebarInitialWidth = 240;
constexpr int kSidebarMinimumExpandedWidth = 188;
constexpr int kSidebarMaximumWidth = 420;
constexpr int kSplitterHandleWidth = 0;
constexpr int kTitleBarHeight = 54;
constexpr int kSidebarToggleInset = 14;

Qt::KeyboardModifier physicalControlModifier() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt maps the physical macOS Control key to MetaModifier; ControlModifier is Command.
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}
} // namespace

RootWidget::RootWidget(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                       AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
                       LanguageManager* languageManager, AppUpdater* updater, QWidget* parent)
    : QWidget(parent) {
    buildUi(repository, recentProjects, assetManager, thumbnailCache, settings, languageManager, updater);
    setupShortcuts();
}

bool RootWidget::confirmClose() {
    return m_editPage->confirmSaveIfDirty();
}

void RootWidget::switchToPage(AppPage page) {
    const int index = static_cast<int>(page);
    if (index >= 0 && index < m_pages->count()) {
        m_pages->setCurrentIndex(index);
        updateSelection(page);
        updatePageMargins(page);
        updateTitleBarForPage(page);
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
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        if (m_sidebarTitleBar) {
            m_sidebarTitleBar->setBackgroundColor(palette().color(QPalette::Window));
        }
        update();
    }
}

void RootWidget::buildUi(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                         AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
                         LanguageManager* languageManager, AppUpdater* updater) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("MainSplitter"));
    m_splitter->setHandleWidth(kSplitterHandleWidth);
    m_splitter->setOpaqueResize(true);
    m_splitter->setStyleSheet(QStringLiteral(
        "QSplitter#MainSplitter::handle{background:transparent;border:none;margin:0;padding:0;width:0px;}"
        "QSplitter#MainSplitter::handle:hover{background:transparent;}"));
    root->addWidget(m_splitter, 1);

    m_sidebarShell = new QFrame(m_splitter);
    m_sidebarShell->setObjectName(QStringLiteral("SidebarShell"));
    m_sidebarShell->setMinimumWidth(0);
    m_sidebarShell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_sidebarShell->setFrameShape(QFrame::NoFrame);

    m_sidebar = createSidebar(m_sidebarShell);
    m_content = createContent(repository, recentProjects, assetManager, thumbnailCache,
                              settings, languageManager, updater);

    m_splitter->addWidget(m_sidebarShell);
    m_splitter->addWidget(m_content);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    connect(m_sidebarView, &QListView::clicked, this, [this](const QModelIndex& index) {
        switchToPage(m_sidebarModel->pageForIndex(index));
    });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int pos, int index) {
        if (m_tierFocusMode) {
            setSidebarWidth(0);
            return;
        }
        m_currentSidebarWidth = m_sidebarShell ? m_sidebarShell->width() : 0;
        if (m_currentSidebarWidth > 0) {
            m_sidebarCollapsed = false;
            m_lastExpandedSidebarWidth = std::max(kSidebarMinimumExpandedWidth, m_currentSidebarWidth);
            if (m_sidebarToggleButton) {
                m_sidebarToggleButton->setChecked(false);
                m_sidebarToggleButton->setToolTip(tr("Collapse sidebar"));
            }
        } else {
            m_sidebarCollapsed = true;
            if (m_sidebarToggleButton) {
                m_sidebarToggleButton->setChecked(true);
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
        if (m_editPage->confirmSaveIfDirty() && m_editPage->openProject(path)) {
            switchToPage(AppPage::Edit);
        }
    });

    connect(m_editPage, &EditPage::titleChanged, this, [this](const QString& title) {
        if (m_pages && m_pages->currentWidget() == m_editPage && !m_tierFocusMode) {
            m_titleBar->setTitleEditable(true);
            m_titleBar->setDocumentTitle(title);
        }
    });
    connect(m_titleBar, &AppTitleBar::newRequested, m_editPage, &EditPage::newProject);
    connect(m_titleBar, &AppTitleBar::openRequested, m_editPage, &EditPage::openProjectFromDialog);
    connect(m_titleBar, &AppTitleBar::saveRequested, m_editPage, &EditPage::saveProject);
    connect(m_titleBar, &AppTitleBar::saveAsRequested, m_editPage, &EditPage::saveProjectAs);
    connect(m_titleBar, &AppTitleBar::backgroundRequested, m_editPage, &EditPage::configureBackground);
    connect(m_titleBar, &AppTitleBar::galleryRequested, m_editPage, &EditPage::toggleGallery);
    connect(m_titleBar, &AppTitleBar::resetRowsRequested, m_editPage, &EditPage::resetRows);
    connect(m_titleBar, &AppTitleBar::projectTitleEdited, m_editPage, &EditPage::renameProject);
    connect(m_titleBar, &AppTitleBar::tierFocusModeRequested, this,
            [this]() { setTierFocusMode(!m_tierFocusMode); });
    connect(m_editPage, &EditPage::galleryMissionControlRequested, this, [this]() {
        if (m_editPage && m_titleBar) {
            m_editPage->toggleGalleryMissionControlMode(m_titleBar->galleryButtonGlobalRect());
        }
    });
    connect(m_editPage, &EditPage::dirtyChanged, m_titleBar, &AppTitleBar::setSaveActionEnabled);
    connect(m_editPage, &EditPage::resetRowsAvailableChanged, m_titleBar,
            &AppTitleBar::setResetRowsActionEnabled);
    m_titleBar->setSaveActionEnabled(m_editPage->isDirty());
    m_titleBar->setResetRowsActionEnabled(false);
    connect(languageManager, &LanguageManager::languageChanged, m_sidebarModel, &SidebarModel::retranslate);
    connect(languageManager, &LanguageManager::languageChanged, this, [this]() {
        if (m_titleBar) {
            m_titleBar->retranslateUi();
        }
        if (m_projectsPage) {
            m_projectsPage->retranslateUi();
        }
        if (m_pages) {
            updateTitleBarForPage(static_cast<AppPage>(m_pages->currentIndex()));
        }
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->setToolTip(m_sidebarCollapsed ? tr("Show sidebar")
                                                                  : tr("Collapse sidebar"));
        }
        if (m_preferencesButton) {
            m_preferencesButton->setToolTip(tr("Preferences"));
        }
    });

    setSidebarWidth(kSidebarInitialWidth);
    switchToPage(AppPage::Edit);
    layoutTitleBars();
    layoutSidebarToggleButton();
    QTimer::singleShot(0, this, [this]() { synchronizeInitialLayout(); });
}

QFrame* RootWidget::createSidebar(QWidget* parent) {
    auto* sidebar = new QFrame(parent);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setStyleSheet(QStringLiteral("QFrame#Sidebar{background:palette(window);border:none;}"));

    m_sidebarLayout = new QVBoxLayout(sidebar);
    m_sidebarLayout->setContentsMargins(12, kTitleBarHeight + 12, 10, 14);
    m_sidebarLayout->setSpacing(12);

    m_sidebarTitleBar = new vkframeless::WindowTitleBar(sidebar);
    m_sidebarTitleBar->setObjectName(QStringLiteral("SidebarTitleBar"));
    m_sidebarTitleBar->setPreferredHeight(kTitleBarHeight);
    m_sidebarTitleBar->setBackgroundColor(palette().color(QPalette::Window));
    m_sidebarTitleBar->setBackgroundOpacity(0.0);
    m_sidebarTitleBar->setBottomSeparatorVisible(false);

    m_sidebarModel = new SidebarModel(this);
    m_sidebarView = new SidebarView(sidebar);
    m_sidebarView->setModel(m_sidebarModel);
    m_sidebarLayout->addWidget(m_sidebarView);
    m_sidebarLayout->addStretch();

    m_preferencesButton = new QToolButton(sidebar);
    m_preferencesButton->setObjectName(QStringLiteral("PreferencesButton"));
    m_preferencesButton->setIcon(QIcon(QStringLiteral(":/icons/preferences.svg")));
    m_preferencesButton->setToolTip(tr("Preferences"));
    m_preferencesButton->setCursor(Qt::PointingHandCursor);
    m_preferencesButton->setFixedSize(34, 34);
    m_preferencesButton->setIconSize(QSize(18, 18));
    m_preferencesButton->setStyleSheet(QStringLiteral(
        "QToolButton#PreferencesButton{border:none;border-radius:8px;background:transparent;}"
        "QToolButton#PreferencesButton:hover{background:rgba(60,80,110,24);}"
        "QToolButton#PreferencesButton:pressed{background:rgba(60,80,110,42);}"));
    connect(m_preferencesButton, &QToolButton::clicked, this, [this]() { switchToPage(AppPage::Preferences); });
    m_sidebarLayout->addWidget(m_preferencesButton, 0, Qt::AlignLeft);

    m_preferencesBadge = new QLabel(m_preferencesButton);
    m_preferencesBadge->setObjectName(QStringLiteral("PreferencesUpdateBadge"));
    m_preferencesBadge->setFixedSize(8, 8);
    m_preferencesBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_preferencesBadge->setStyleSheet(QStringLiteral(
        "QLabel#PreferencesUpdateBadge{background:#ff4f5f;border-radius:4px;}"));
    m_preferencesBadge->hide();
    layoutPreferenceBadge();
    return sidebar;
}

QFrame* RootWidget::createContent(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                                  AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                                  AppSettings* settings, LanguageManager* languageManager,
                                  AppUpdater* updater) {
    auto* content = new QFrame(m_splitter);
    content->setObjectName(QStringLiteral("Content"));
    content->setMinimumWidth(620);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setStyleSheet(QStringLiteral("QFrame#Content{background:palette(base);}"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_pages = new QStackedWidget(content);
    m_pages->setObjectName(QStringLiteral("Pages"));
    m_pages->setContentsMargins(0, 0, 0, 0);
    m_editPage = new EditPage(repository, recentProjects, assetManager, thumbnailCache, settings, content);
    m_projectsPage = new ProjectsPage(repository, recentProjects, content);
    m_preferencesPage = new PreferencesPage(settings, languageManager, updater, content);
    m_pages->addWidget(m_editPage);
    m_pages->addWidget(m_projectsPage);
    m_pages->addWidget(m_preferencesPage);
    contentLayout->addWidget(m_pages, 1);

    m_titleBar = new AppTitleBar(content);
    m_titleBar->setGeometry(0, 0, content->width(), kTitleBarHeight);
    m_titleBar->raise();
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
    m_sidebarToggleButton->setChecked(collapsed);
    m_sidebarToggleButton->setToolTip(collapsed ? tr("Show sidebar") : tr("Collapse sidebar"));
    layoutSidebarToggleButton();

    if (m_sidebarShell) {
        m_sidebarShell->show();
    }
    if (m_sidebar) {
        m_sidebar->show();
    }

    const int startWidth = m_currentSidebarWidth;
    const int endWidth = collapsed ? 0 : std::max(kSidebarMinimumExpandedWidth, m_lastExpandedSidebarWidth);
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

    const int totalWidth = std::max(width + (m_content ? m_content->minimumWidth() : 620),
                                    m_splitter->width());
    const int maxSidebarWidth = std::clamp(totalWidth - (m_content ? m_content->minimumWidth() : 620),
                                           kSidebarMinimumExpandedWidth, kSidebarMaximumWidth);
    const int sidebarWidth = std::clamp(width, 0, maxSidebarWidth);

    m_sidebarShell->show();
    m_sidebar->show();
    m_currentSidebarWidth = sidebarWidth;
    m_sidebarShell->setMaximumWidth(sidebarWidth == 0 ? 0 : QWIDGETSIZE_MAX);
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

    const int targetWidth = m_sidebarCollapsed ? 0 : std::max(kSidebarMinimumExpandedWidth,
                                                              m_lastExpandedSidebarWidth);
    setSidebarWidth(targetWidth);
    if (m_pages) {
        const auto page = static_cast<AppPage>(m_pages->currentIndex());
        updatePageMargins(page);
        updateTitleBarForPage(page);
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
    Logger::info(QStringLiteral("ui.tier.focus.mode enabled=%1").arg(enabled));

    if (enabled) {
        if (auto* frameless = qobject_cast<vkframeless::FramelessWindow*>(window())) {
            m_savedSystemButtonVisibility = frameless->systemButtonVisibility();
            frameless->setSystemButtonVisibility(vkframeless::SystemButtonVisibility::Hidden);
        }
        m_focusSavedSidebarCollapsed = m_sidebarCollapsed || m_currentSidebarWidth <= 0;
        m_focusSavedSidebarWidth = m_focusSavedSidebarCollapsed
                                       ? std::max(kSidebarMinimumExpandedWidth, m_lastExpandedSidebarWidth)
                                       : std::max(kSidebarMinimumExpandedWidth, m_currentSidebarWidth);
        if (m_pages) {
            m_pages->setContentsMargins(0, 0, 0, 0);
        }
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->hide();
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
        if (auto* frameless = qobject_cast<vkframeless::FramelessWindow*>(window())) {
            frameless->setSystemButtonVisibility(m_savedSystemButtonVisibility);
        }
        if (m_pages) {
            updatePageMargins(static_cast<AppPage>(m_pages->currentIndex()));
        }
        if (m_titleBar) {
            m_titleBar->show();
            m_titleBar->setTierFocusMode(false);
        }
        if (m_editPage) {
            m_editPage->setTierFocusMode(false);
        }
        m_sidebarCollapsed = m_focusSavedSidebarCollapsed;
        setSidebarWidth(m_sidebarCollapsed ? 0
                                           : std::clamp(m_focusSavedSidebarWidth,
                                                        kSidebarMinimumExpandedWidth, kSidebarMaximumWidth));
        if (m_sidebarToggleButton) {
            m_sidebarToggleButton->show();
            m_sidebarToggleButton->setChecked(m_sidebarCollapsed);
            m_sidebarToggleButton->setToolTip(m_sidebarCollapsed ? tr("Show sidebar") : tr("Collapse sidebar"));
        }
        if (m_pages) {
            updateTitleBarForPage(static_cast<AppPage>(m_pages->currentIndex()));
        }
    }

    layoutTitleBars();
    layoutSidebarToggleButton();
}

void RootWidget::syncSidebarPresentation(int sidebarWidth) {
    if (!m_sidebarView) {
        return;
    }
    m_sidebarView->setVisible(sidebarWidth > 48);
}

void RootWidget::layoutSidebarSurface() {
    if (!m_sidebarShell || !m_sidebar) {
        return;
    }

    // The shell owns the animated width. The real sidebar remains expanded and is clipped by the shell,
    // matching the VKFrameless demo and avoiding QSplitter minimum-size jumps.
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
            const int revealWidth = std::min(contentWidth, m_titleBar->focusRevealSizeHint().width());
            m_titleBar->setGeometry(std::max(0, contentWidth - revealWidth), 0, revealWidth,
                                    kTitleBarHeight);
        } else {
            m_titleBar->setGeometry(0, 0, contentWidth, kTitleBarHeight);
        }
        m_titleBar->raise();
    }
    if (m_sidebarToggleButton) {
        m_sidebarToggleButton->raise();
    }
}

void RootWidget::layoutSidebarToggleButton() {
    if (!m_sidebarToggleButton) {
        return;
    }

    const int buttonWidth = m_sidebarToggleButton->width();
    const int minimumX = minimumSidebarToggleX();
    const int titleBarWidth = m_sidebarTitleBar ? m_sidebarTitleBar->width() : width();
    const int maximumX = std::max(minimumX, titleBarWidth - buttonWidth - kSidebarToggleInset);
    const int naturalX = m_currentSidebarWidth > 0
                             ? m_currentSidebarWidth - buttonWidth - kSidebarToggleInset
                             : minimumX;
    const int x = std::clamp(std::max(naturalX, minimumX), minimumX, maximumX);
    const int y = std::max(0, (kTitleBarHeight - m_sidebarToggleButton->height()) / 2);
    m_sidebarToggleButton->move(x, y);
    m_sidebarToggleButton->raise();
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

int RootWidget::minimumSidebarToggleX() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (auto* frameless = qobject_cast<vkframeless::FramelessWindow*>(window())) {
        if (frameless->systemButtonVisibility() != vkframeless::SystemButtonVisibility::Hidden) {
            return frameless->systemButtonReservedWidth() + 8;
        }
    }
#endif
    return kSidebarToggleInset;
}

void RootWidget::setupShortcuts() {
    auto addShortcut = [this](const QKeySequence& sequence, const QObject* receiver, const char* slot) {
        auto* shortcut = new QShortcut(sequence, this);
        connect(shortcut, SIGNAL(activated()), receiver, slot);
    };
    addShortcut(QKeySequence::New, m_editPage, SLOT(newProject()));
    addShortcut(QKeySequence::Open, m_editPage, SLOT(openProjectFromDialog()));
    auto* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, [this]() {
        if (m_editPage && m_editPage->isDirty()) {
            m_editPage->saveProject();
        }
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), m_editPage, SLOT(saveProjectAs()));
    auto* missionShortcut = new QShortcut(QKeySequence(QKeyCombination(physicalControlModifier(), Qt::Key_I)), this);
    connect(missionShortcut, &QShortcut::activated, m_editPage, &EditPage::toggleMissionControlMode);
    auto* galleryMissionShortcut = new QShortcut(QKeySequence(QKeyCombination(physicalControlModifier(), Qt::Key_P)), this);
    connect(galleryMissionShortcut, &QShortcut::activated, this, [this]() {
        if (m_editPage && m_titleBar) {
            m_editPage->toggleGalleryMissionControlMode(m_titleBar->galleryButtonGlobalRect());
        }
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), m_editPage, SLOT(exportProjectFromDialog()));

    auto* preferencesShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this);
    connect(preferencesShortcut, &QShortcut::activated, this, [this]() { switchToPage(AppPage::Preferences); });
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
        m_titleBar->setTitleEditable(true);
        m_titleBar->setDocumentTitle(m_editPage ? m_editPage->displayTitle() : QString());
        break;
    case AppPage::Projects:
        m_titleBar->setEditorActionsVisible(false);
        m_titleBar->setTitleEditable(false);
        m_titleBar->setDocumentTitle(tr("Projects"));
        break;
    case AppPage::Preferences:
        m_titleBar->setEditorActionsVisible(false);
        m_titleBar->setTitleEditable(false);
        m_titleBar->setDocumentTitle(tr("Preferences"));
        break;
    }
}

void RootWidget::updatePageMargins(AppPage page) {
    if (!m_pages) {
        return;
    }
    if (m_tierFocusMode || page == AppPage::Edit) {
        // The edit page lets the transparent title bar float above the board, so no
        // reserved content strip can cover the tier-list shadow at the top edge.
        m_pages->setContentsMargins(0, 0, 0, 0);
        return;
    }
    m_pages->setContentsMargins(0, kTitleBarHeight, 0, 0);
}

} // namespace tlm
