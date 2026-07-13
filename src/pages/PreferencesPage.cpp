#include "pages/PreferencesPage.h"

#include "platform/Platform.h"
#include "theme/Theme.h"
#include "widgets/SectionHeader.h"

#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSysInfo>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <vkui/core/VkIcon.h>
#include <vkui/widgets/VkComboBox.h>
#include <vkui/widgets/controls/VkSwitch.h>

#ifndef TLM_APP_VERSION
#define TLM_APP_VERSION "0.1.0"
#endif

#ifndef TLM_GIT_COMMIT
#define TLM_GIT_COMMIT "unknown"
#endif

namespace tlm {

namespace {
template <typename Enum> int enumIndex(Enum value) {
    return static_cast<int>(value);
}

void configureAdaptiveField(QWidget* widget) {
    if (!widget) {
        return;
    }
    if (qobject_cast<vkui::VkSwitch*>(widget)) {
        widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return;
    }
    if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        comboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        vkui::setComboBoxElideMode(*comboBox, Qt::ElideRight);
    }
    widget->setMinimumWidth(164);
    widget->setMaximumWidth(260);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void setSettingRowExpanded(QWidget* row, bool expanded, bool animated) {
    if (!row) {
        return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(row->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(row);
        row->setGraphicsEffect(effect);
    }

    const int expandedHeight = qMax(1, row->sizeHint().height());
    if (!animated) {
        row->setVisible(expanded);
        row->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
        effect->setOpacity(expanded ? 1.0 : 0.0);
        return;
    }

    row->setVisible(true);
    const qreal start = effect->opacity();
    const qreal end = expanded ? 1.0 : 0.0;
    auto* animation = new QVariantAnimation(row);
    animation->setDuration(180);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(start);
    animation->setEndValue(end);
    QObject::connect(animation, &QVariantAnimation::valueChanged, row,
                     [row, effect, expandedHeight](const QVariant& value) {
                         const qreal progress = value.toReal();
                         effect->setOpacity(progress);
                         row->setMaximumHeight(qMax(0, qRound(expandedHeight * progress)));
                     });
    QObject::connect(animation, &QVariantAnimation::finished, row,
                     [row, effect, expanded, animation]() {
                         effect->setOpacity(expanded ? 1.0 : 0.0);
                         row->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
                         row->setVisible(expanded);
                         animation->deleteLater();
                     });
    animation->start();
}

QWidget* createSettingRow(const QString& title, QWidget* control, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    auto* label = new QLabel(title, row);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    label->setMinimumWidth(120);
    layout->addWidget(label);
    layout->addStretch(1);

    configureAdaptiveField(control);
    layout->addWidget(control, 0, Qt::AlignRight);
    return row;
}
} // namespace

PreferencesPage::PreferencesPage(AppSettings* settings, LanguageManager* languageManager,
                                 AppUpdater* updater, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_languageManager(languageManager),
      m_updater(updater) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 24);
    root->setSpacing(0);

    m_pageContainer = new QWidget(this);
    m_pageContainer->setObjectName(QStringLiteral("PreferencesPageContainer"));
    m_pageContainer->setMaximumWidth(760);
    m_pageContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_sectionsLayout = new QVBoxLayout(m_pageContainer);
    m_sectionsLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionsLayout->setSpacing(26);
    root->addWidget(m_pageContainer, 0, Qt::AlignHCenter | Qt::AlignTop);
    root->addStretch(1);

    rebuildPreferencePages();
    if (m_updater) {
        connect(m_updater, &AppUpdater::checkingStarted, this, [this](const QUrl& url) {
            if (m_updateStatusLabel) {
                m_updateStatusLabel->setText(tr("Checking updates from:\n%1").arg(url.toString()));
            }
            if (m_checkUpdateButton) {
                m_checkUpdateButton->setEnabled(false);
            }
            if (m_openUpdateButton) {
                m_openUpdateButton->setEnabled(false);
            }
        });
        connect(m_updater, &AppUpdater::updateAvailable, this, &PreferencesPage::applyUpdateResult);
        connect(m_updater, &AppUpdater::noUpdateAvailable, this,
                &PreferencesPage::applyUpdateResult);
        connect(m_updater, &AppUpdater::checkFailed, this, &PreferencesPage::applyUpdateFailure);
        connect(m_updater, &AppUpdater::updateNotificationChanged, this,
                &PreferencesPage::setUpdateNotificationVisible);
        setUpdateNotificationVisible(m_updater->hasUpdateAvailable());
    }
    if (m_languageManager) {
        connect(m_languageManager, &LanguageManager::languageChanged, this,
                &PreferencesPage::retranslateUi);
    }
}

void PreferencesPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
        refreshPreferenceControlStyles();
    }
}

void PreferencesPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

void PreferencesPage::retranslateUi() {
    rebuildPreferencePages();
}

void PreferencesPage::setUpdateNotificationVisible(bool visible) {
    if (m_updateStatusLabel && visible && m_updateStatusLabel->text().trimmed().isEmpty()) {
        m_updateStatusLabel->setText(tr("An update is available."));
    }
}

void PreferencesPage::rebuildPreferencePages() {
    if (!m_sectionsLayout) {
        return;
    }
    while (QLayoutItem* item = m_sectionsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_updateStatusLabel = nullptr;
    m_checkUpdateButton = nullptr;
    m_openUpdateButton = nullptr;
    m_sectionsLayout->addWidget(createGeneralPage());
    m_sectionsLayout->addWidget(createUpdatePage());
    m_sectionsLayout->addWidget(createAboutPage());
}

void PreferencesPage::refreshPreferenceControlStyles() {
    update();
}

void PreferencesPage::applyUpdateResult(const UpdateCheckResult& result) {
    m_lastUpdateResult = result;
    setUpdateNotificationVisible(result.updateAvailable);
    if (m_updateStatusLabel) {
        if (result.updateAvailable) {
            const QString changelog = result.changelog.trimmed();
            m_updateStatusLabel->setText(tr("Version %1 is available. Current version: %2%3")
                                             .arg(result.latestVersion, result.currentVersion,
                                                  changelog.isEmpty()
                                                      ? QString()
                                                      : QStringLiteral("\n\n%1").arg(changelog)));
        } else {
            m_updateStatusLabel->setText(
                tr("TierListMaker is up to date. Current version: %1").arg(result.currentVersion));
        }
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
    }
    if (m_openUpdateButton) {
        m_openUpdateButton->setEnabled(result.updateAvailable);
    }
}

void PreferencesPage::applyUpdateFailure(const QString& reason) {
    if (m_updateStatusLabel) {
        m_updateStatusLabel->setText(
            tr("Update check failed: %1\nThe project may not have published update metadata yet.")
                .arg(reason));
    }
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
    }
    if (m_openUpdateButton) {
        m_openUpdateButton->setEnabled(true);
    }
}

QWidget* PreferencesPage::createGeneralPage() {
    auto* page = new QWidget(this);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 0, 0, 0);
    layout->setSpacing(16);

    auto* settingsLayout = new QVBoxLayout;
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(12);

    auto* language = new QComboBox(page);
    language->addItem(tr("System"), QStringLiteral("system"));
    language->addItem(tr("English"), QStringLiteral("en"));
    language->addItem(tr("Simplified Chinese"), QStringLiteral("zh_CN"));
    language->setCurrentIndex(qMax(0, language->findData(m_settings->language())));
    connect(language, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, language](int index) {
                const QString code = language->itemData(index).toString();
                m_settings->setLanguage(code);
                if (m_languageManager) {
                    m_languageManager->setLanguage(code);
                }
            });
    settingsLayout->addWidget(createSettingRow(tr("Language"), language, page));

    auto* appearance = new QComboBox(page);
    appearance->addItems({tr("System"), tr("Light"), tr("Dark")});
    appearance->setCurrentIndex(enumIndex(m_settings->appearance()));
    connect(appearance, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { m_settings->setAppearance(static_cast<AppearanceMode>(index)); });
    settingsLayout->addWidget(createSettingRow(tr("Appearance"), appearance, page));

    auto* importBehavior = new QComboBox(page);
    importBehavior->addItems({tr("Copy images into project folder"), tr("Reference original path"),
                              tr("Ask every time")});
    importBehavior->setCurrentIndex(enumIndex(m_settings->importBehavior()));
    connect(importBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                m_settings->setImportBehavior(static_cast<ImageImportBehavior>(index));
            });
    settingsLayout->addWidget(createSettingRow(tr("Image import behavior"), importBehavior, page));

    const auto addBlankAreaActionItems = [](QComboBox* combo) {
        combo->addItem(tr("Open Gallery Overview"),
                       static_cast<int>(BlankAreaAction::GalleryMissionControl));
        combo->addItem(tr("Open Tier Overview"),
                       static_cast<int>(BlankAreaAction::TierMissionControl));
        combo->addItem(tr("Do Nothing"), static_cast<int>(BlankAreaAction::None));
    };
    const auto setBlankAreaActionIndex = [](QComboBox* combo, BlankAreaAction action) {
        const int index = combo->findData(static_cast<int>(action));
        combo->setCurrentIndex(qMax(0, index));
    };

    auto* blankDoubleClick = new QComboBox(page);
    addBlankAreaActionItems(blankDoubleClick);
    setBlankAreaActionIndex(blankDoubleClick, m_settings->blankDoubleClickAction());
    connect(blankDoubleClick, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, blankDoubleClick](int index) {
                m_settings->setBlankDoubleClickAction(
                    static_cast<BlankAreaAction>(blankDoubleClick->itemData(index).toInt()));
            });
    settingsLayout->addWidget(
        createSettingRow(tr("Empty tier area double-click"), blankDoubleClick, page));

    auto* blankLongPress = new QComboBox(page);
    addBlankAreaActionItems(blankLongPress);
    setBlankAreaActionIndex(blankLongPress, m_settings->blankLongPressAction());
    connect(blankLongPress, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, blankLongPress](int index) {
                m_settings->setBlankLongPressAction(
                    static_cast<BlankAreaAction>(blankLongPress->itemData(index).toInt()));
            });
    settingsLayout->addWidget(
        createSettingRow(tr("Empty tier area long press"), blankLongPress, page));

    auto* autosave = new vkui::VkSwitch(page);
    autosave->setAccessibleName(tr("Enable autosave"));
    autosave->setChecked(m_settings->autosaveEnabled());
    settingsLayout->addWidget(createSettingRow(tr("Save behavior"), autosave, page));

    auto* autosaveInterval = new QSpinBox(page);
    autosaveInterval->setRange(1, 60);
    autosaveInterval->setSuffix(tr(" min"));
    autosaveInterval->setValue(m_settings->autosaveIntervalMinutes());
    connect(autosaveInterval, QOverload<int>::of(&QSpinBox::valueChanged), m_settings,
            &AppSettings::setAutosaveIntervalMinutes);
    QWidget* autosaveIntervalRow =
        createSettingRow(tr("Autosave interval"), autosaveInterval, page);
    settingsLayout->addWidget(autosaveIntervalRow);
    setSettingRowExpanded(autosaveIntervalRow, autosave->isChecked(), false);
    connect(autosave, &QAbstractButton::toggled, this, [this, autosaveIntervalRow](bool enabled) {
        m_settings->setAutosaveEnabled(enabled);
        setSettingRowExpanded(autosaveIntervalRow, enabled, true);
    });

    auto* projectFolderRow = new QWidget(page);
    auto* projectFolderLayout = new QHBoxLayout(projectFolderRow);
    projectFolderLayout->setContentsMargins(0, 0, 0, 0);
    projectFolderLayout->setSpacing(18);
    auto* projectFolderLabel = new QLabel(tr("Default project folder"), projectFolderRow);
    projectFolderLabel->setMinimumWidth(120);
    auto* projectFolderControl = new QWidget(projectFolderRow);
    auto* projectFolderControlLayout = new QHBoxLayout(projectFolderControl);
    projectFolderControlLayout->setContentsMargins(0, 0, 0, 0);
    projectFolderControlLayout->setSpacing(8);
    auto* projectFolderEdit = new QLineEdit(m_settings->defaultProjectDirectory(), projectFolderControl);
    projectFolderEdit->setReadOnly(true);
    projectFolderEdit->setMinimumWidth(260);
    auto* chooseProjectFolder =
        new QPushButton(vkui::icon(vkui::VkSymbol::Folder), tr("Choose"), projectFolderControl);
    projectFolderControlLayout->addWidget(projectFolderEdit, 1);
    projectFolderControlLayout->addWidget(chooseProjectFolder);
    projectFolderLayout->addWidget(projectFolderLabel);
    projectFolderLayout->addStretch(1);
    projectFolderLayout->addWidget(projectFolderControl);
    connect(chooseProjectFolder, &QPushButton::clicked, this, [this, projectFolderEdit]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Default Project Folder"), m_settings->defaultProjectDirectory(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!directory.isEmpty()) {
            m_settings->setDefaultProjectDirectory(directory);
            projectFolderEdit->setText(m_settings->defaultProjectDirectory());
        }
    });
    settingsLayout->addWidget(projectFolderRow);

    auto* autoUpdate = new vkui::VkSwitch(page);
    autoUpdate->setAccessibleName(tr("Enable automatic update checks"));
    autoUpdate->setChecked(m_settings->autoUpdateEnabled());
    connect(autoUpdate, &QAbstractButton::toggled, this, [this](bool enabled) {
        m_settings->setAutoUpdateEnabled(enabled);
        if (enabled && m_updater && !m_updater->isChecking()) {
            m_updater->checkForUpdates();
        }
    });
    settingsLayout->addWidget(createSettingRow(tr("Updates"), autoUpdate, page));

    layout->addWidget(new SectionHeader(tr("General"), page));
    layout->addLayout(settingsLayout);
    layout->addStretch();
    return page;
}

QWidget* PreferencesPage::createUpdatePage() {
    auto* page = new QWidget(this);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new SectionHeader(tr("Updates"), page));
    auto* endpoint = new QLabel(
        tr("Update definition:\n%1").arg(AppUpdater::defaultUpdateDefinitionUrl().toString()),
        page);
    endpoint->setWordWrap(true);
    endpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(endpoint);

    m_updateStatusLabel =
        new QLabel(tr("Manual checks run when you click Check Now. Automatic checks run at most "
                      "once per day when "
                      "enabled. No update is downloaded or executed automatically."),
                   page);
    m_updateStatusLabel->setWordWrap(true);
    m_updateStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_updateStatusLabel);

    auto* actions = new QHBoxLayout;
    m_checkUpdateButton =
        new QPushButton(vkui::icon(vkui::VkSymbol::Download), tr("Check Now"), page);
    m_openUpdateButton =
        new QPushButton(vkui::icon(vkui::VkSymbol::Share), tr("Open Release Page"), page);
    m_openUpdateButton->setEnabled(m_updater && m_updater->hasUpdateAvailable());
    actions->addWidget(m_checkUpdateButton);
    actions->addWidget(m_openUpdateButton);
    actions->addStretch();
    layout->addLayout(actions);

    connect(m_checkUpdateButton, &QPushButton::clicked, this, [this]() {
        if (m_updater) {
            m_updater->checkForUpdates();
        }
    });
    connect(m_openUpdateButton, &QPushButton::clicked, this, [this]() {
        if (m_updater) {
            m_updater->openUpdatePage(m_lastUpdateResult);
        }
    });

    if (m_updater && m_updater->lastResult().latestVersion.size() > 0) {
        applyUpdateResult(m_updater->lastResult());
    }

    layout->addStretch();
    return page;
}

QWidget* PreferencesPage::createAboutPage() {
    auto* page = new QWidget(this);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new SectionHeader(tr("About"), page));
    auto* text =
        new QLabel(tr("TierListMaker\nVersion: %1\nLicense: MIT\nQt: %2\nQWindowKit (dev) and "
                      "VkUI: linked dependencies\nBuild: %3\nGit: "
                      "%4\nPlatform: %5\nCopyright: 2026 TierListMaker "
                      "contributors\n\nLinks:\nhttps://github.com/"
                      "qianvk/TierListMaker")
                       .arg(QStringLiteral(TLM_APP_VERSION), QString::fromLatin1(qVersion()),
#ifdef NDEBUG
                            QStringLiteral("Release"),
#else
                            QStringLiteral("Debug"),
#endif
                            QStringLiteral(TLM_GIT_COMMIT), platform::platformName()),
                   page);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addStretch();
    return page;
}

} // namespace tlm
