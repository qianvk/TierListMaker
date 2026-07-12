#include "theme/ThemeManager.h"

#include <QApplication>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>

namespace tlm {

ThemeManager::ThemeManager(AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings),
      m_generation(vkui::VkThemeManager::instance()->theme().generation()) {
    connect(m_settings, &AppSettings::appearanceChanged, this, [this](AppearanceMode) {
        if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
            applyTo(*app);
        }
    });
    connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
            [this](quint64) { synchronizeTheme(); });
}

void ThemeManager::applyTo(QApplication& app) {
    Q_UNUSED(app);
    vkui::VkAppearance appearance = vkui::VkAppearance::Auto;
    if (m_settings) {
        switch (m_settings->appearance()) {
        case AppearanceMode::Light:
            appearance = vkui::VkAppearance::Light;
            break;
        case AppearanceMode::Dark:
            appearance = vkui::VkAppearance::Dark;
            break;
        case AppearanceMode::System:
            appearance = vkui::VkAppearance::Auto;
            break;
        }
    }
    vkui::VkThemeManager::instance()->setAppearance(appearance);
    synchronizeTheme();
}

void ThemeManager::synchronizeTheme() {
    const vkui::VkTheme& vkTheme = vkui::VkThemeManager::instance()->theme();
    if (m_generation == vkTheme.generation()) {
        return;
    }
    m_generation = vkTheme.generation();
    m_theme = Theme(vkTheme);
    emit themeChanged(m_theme);
}

} // namespace tlm
