#include "pages/PreferencesPage.h"

#include "platform/Platform.h"
#include "widgets/RoundedButton.h"
#include "widgets/SectionHeader.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#ifndef TLM_APP_VERSION
#define TLM_APP_VERSION "0.1.0"
#endif

#ifndef TLM_GIT_COMMIT
#define TLM_GIT_COMMIT "unknown"
#endif

namespace tlm {

namespace {
constexpr auto kPreferenceControlProperty = "tlmPreferenceControl";
constexpr int kUpdateBadgeRole = Qt::UserRole + 41;

template <typename Enum>
int enumIndex(Enum value) {
    return static_cast<int>(value);
}

class PreferencesNavDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRect r = option.rect.adjusted(6, 3, -6, -3);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        if (selected || hovered) {
            QColor fill = selected ? option.palette.highlight().color() : option.palette.midlight().color();
            fill.setAlpha(selected ? 70 : 35);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(r, 8, 8);
        }

        painter->setPen(option.palette.color(QPalette::WindowText));
        painter->drawText(r.adjusted(14, 0, -30, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());

        if (index.data(kUpdateBadgeRole).toBool()) {
            const int d = 8;
            const QRect badge(r.right() - d - 12, r.center().y() - d / 2, d, d);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(QStringLiteral("#ff4f5f")));
            painter->drawEllipse(badge);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(160, 38);
    }
};

void configureAdaptiveField(QWidget* widget) {
    if (!widget) {
        return;
    }
    widget->setMinimumWidth(164);
    widget->setMaximumWidth(260);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void applyPreferenceControlStyle(QWidget* widget) {
    if (!widget) {
        return;
    }

    widget->setStyleSheet({});
    QPalette palette = QApplication::palette(widget);
    if (palette.color(QPalette::Base).lightness() < 96) {
        const QColor control = palette.color(QPalette::AlternateBase);
        palette.setColor(QPalette::Base, control);
        palette.setColor(QPalette::Button, control);
        palette.setColor(QPalette::Window, control);
        palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
    }
    // Palette-only updates keep native geometry, indicator marks, and popup metrics
    // identical across appearances while still letting dark colors take effect.
    widget->setPalette(palette);
}

void markPreferenceControl(QWidget* widget) {
    if (!widget) {
        return;
    }
    widget->setProperty(kPreferenceControlProperty, true);
    applyPreferenceControlStyle(widget);
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
    QObject::connect(animation, &QVariantAnimation::finished, row, [row, effect, expanded, animation]() {
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
    markPreferenceControl(control);
    layout->addWidget(control, 0, Qt::AlignRight);
    return row;
}
} // namespace

PreferencesPage::PreferencesPage(AppSettings* settings, LanguageManager* languageManager, AppUpdater* updater,
                                 QWidget* parent)
    : QWidget(parent),
      m_settings(settings),
      m_languageManager(languageManager),
      m_updater(updater) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(14);

    auto* body = new QHBoxLayout;
    m_nav = new QListWidget(this);
    m_nav->addItems({tr("General"), tr("Updates"), tr("About")});
    m_nav->setMinimumWidth(150);
    m_nav->setMaximumWidth(300);
    m_nav->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setCurrentRow(0);
    m_nav->setItemDelegate(new PreferencesNavDelegate(m_nav));
    m_nav->setStyleSheet(QStringLiteral("QListWidget{background:transparent;outline:0;border:none;}"));

    m_stack = new QStackedWidget(this);
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack->addWidget(createGeneralPage());
    m_stack->addWidget(createUpdatePage());
    m_stack->addWidget(createAboutPage());

    body->addWidget(m_nav);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
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
        connect(m_updater, &AppUpdater::noUpdateAvailable, this, &PreferencesPage::applyUpdateResult);
        connect(m_updater, &AppUpdater::checkFailed, this, &PreferencesPage::applyUpdateFailure);
        connect(m_updater, &AppUpdater::updateNotificationChanged, this,
                &PreferencesPage::setUpdateNotificationVisible);
        setUpdateNotificationVisible(m_updater->hasUpdateAvailable());
    }
    if (m_languageManager) {
        connect(m_languageManager, &LanguageManager::languageChanged, this, &PreferencesPage::retranslateUi);
    }
    updateNavWidth();
    QTimer::singleShot(0, this, &PreferencesPage::updateNavWidth);
}

void PreferencesPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        refreshPreferenceControlStyles();
    }
}

void PreferencesPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateNavWidth();
}

void PreferencesPage::retranslateUi() {
    if (!m_nav || !m_stack) {
        return;
    }
    const int row = qBound(0, m_nav->currentRow(), 2);
    m_nav->item(0)->setText(tr("General"));
    m_nav->item(1)->setText(tr("Updates"));
    m_nav->item(2)->setText(tr("About"));
    rebuildPreferencePages();
    m_nav->setCurrentRow(row);
    m_stack->setCurrentIndex(row);
    updateNavWidth();
}

void PreferencesPage::setUpdateNotificationVisible(bool visible) {
    if (!m_nav || m_nav->count() < 2) {
        return;
    }
    m_nav->item(1)->setData(kUpdateBadgeRole, visible);
    m_nav->viewport()->update();
}

void PreferencesPage::rebuildPreferencePages() {
    while (m_stack && m_stack->count() > 0) {
        QWidget* page = m_stack->widget(0);
        m_stack->removeWidget(page);
        page->deleteLater();
    }
    m_stack->addWidget(createGeneralPage());
    m_stack->addWidget(createUpdatePage());
    m_stack->addWidget(createAboutPage());
}

void PreferencesPage::updateNavWidth() {
    if (!m_nav) {
        return;
    }
    QFontMetrics metrics(m_nav->font());
    int width = 150;
    for (int i = 0; i < m_nav->count(); ++i) {
        width = qMax(width, metrics.horizontalAdvance(m_nav->item(i)->text()) + 62);
    }
    const int availableWidth = qMax(1, this->width());
    const int maximumNavWidth = qBound(190, availableWidth / 3, 300);
    const int navWidth = qBound(150, width, maximumNavWidth);
    m_nav->setFixedWidth(navWidth);
    if (m_stack) {
        const int contentWidth = qBound(320, availableWidth - navWidth - 86, 760);
        m_stack->setMinimumWidth(contentWidth);
        for (int i = 0; i < m_stack->count(); ++i) {
            if (QWidget* page = m_stack->widget(i)) {
                page->setMinimumWidth(contentWidth);
            }
        }
    }
}

void PreferencesPage::refreshPreferenceControlStyles() {
    const auto controls = findChildren<QWidget*>();
    for (QWidget* widget : controls) {
        if (widget && widget->property(kPreferenceControlProperty).toBool()) {
            applyPreferenceControlStyle(widget);
        }
    }
}

void PreferencesPage::applyUpdateResult(const UpdateCheckResult& result) {
    m_lastUpdateResult = result;
    setUpdateNotificationVisible(result.updateAvailable);
    if (m_updateStatusLabel) {
        if (result.updateAvailable) {
            const QString changelog = result.changelog.trimmed();
            m_updateStatusLabel->setText(
                tr("Version %1 is available. Current version: %2%3")
                    .arg(result.latestVersion, result.currentVersion,
                         changelog.isEmpty() ? QString() : QStringLiteral("\n\n%1").arg(changelog)));
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
    connect(language, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, language](int index) {
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
    importBehavior->addItems(
        {tr("Copy images into project folder"), tr("Reference original path"), tr("Ask every time")});
    importBehavior->setCurrentIndex(enumIndex(m_settings->importBehavior()));
    connect(importBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                m_settings->setImportBehavior(static_cast<ImageImportBehavior>(index));
            });
    settingsLayout->addWidget(createSettingRow(tr("Image import behavior"), importBehavior, page));

    auto* autosave = new QCheckBox(tr("Enable autosave"), page);
    autosave->setChecked(m_settings->autosaveEnabled());
    settingsLayout->addWidget(createSettingRow(tr("Save behavior"), autosave, page));

    auto* autosaveInterval = new QSpinBox(page);
    autosaveInterval->setRange(1, 60);
    autosaveInterval->setSuffix(tr(" min"));
    autosaveInterval->setValue(m_settings->autosaveIntervalMinutes());
    connect(autosaveInterval, QOverload<int>::of(&QSpinBox::valueChanged), m_settings,
            &AppSettings::setAutosaveIntervalMinutes);
    QWidget* autosaveIntervalRow = createSettingRow(tr("Autosave interval"), autosaveInterval, page);
    settingsLayout->addWidget(autosaveIntervalRow);
    setSettingRowExpanded(autosaveIntervalRow, autosave->isChecked(), false);
    connect(autosave, &QCheckBox::toggled, this, [this, autosaveIntervalRow](bool enabled) {
        m_settings->setAutosaveEnabled(enabled);
        setSettingRowExpanded(autosaveIntervalRow, enabled, true);
    });

    auto* autoUpdate = new QCheckBox(tr("Enable automatic update checks"), page);
    autoUpdate->setChecked(m_settings->autoUpdateEnabled());
    connect(autoUpdate, &QCheckBox::toggled, this, [this](bool enabled) {
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
        tr("Update definition:\n%1").arg(AppUpdater::defaultUpdateDefinitionUrl().toString()), page);
    endpoint->setWordWrap(true);
    endpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(endpoint);

    m_updateStatusLabel = new QLabel(
        tr("Manual checks run when you click Check Now. Automatic checks run at most once per day when "
           "enabled. No update is downloaded or executed automatically."),
        page);
    m_updateStatusLabel->setWordWrap(true);
    m_updateStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_updateStatusLabel);

    auto* actions = new QHBoxLayout;
    m_checkUpdateButton = new RoundedButton(tr("Check Now"), page);
    m_openUpdateButton = new RoundedButton(tr("Open Release Page"), page);
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
    auto* text = new QLabel(
        tr("TierListMaker\nVersion: %1\nLicense: MIT\nQt: %2\nVKFrameless: linked dependency\nBuild: %3\nGit: "
           "%4\nPlatform: %5\nCopyright: 2026 TierListMaker contributors\n\nLinks:\nhttps://github.com/"
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
