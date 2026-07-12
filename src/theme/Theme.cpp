#include "theme/Theme.h"

#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>

#include <cmath>
#include <limits>

namespace tlm {

Theme::Theme() : Theme(vkui::VkThemeManager::instance()->theme()) {}

Theme::Theme(const vkui::VkTheme& theme)
    : m_kind(theme.effectiveAppearance() == vkui::VkAppearance::Dark ? Kind::Dark : Kind::Light) {
    const vkui::VkColorTokens& colors = theme.colors();
    m_tokens.windowBackground = colors.windowBackground;
    m_tokens.sidebarBackground = colors.windowBackground;
    m_tokens.contentBackground = colors.contentBackground;
    m_tokens.elevatedBackground = colors.elevatedBackground;
    m_tokens.popoverBackground = colors.popoverBackground;
    m_tokens.controlFill = colors.controlFill;
    m_tokens.controlFillHovered = colors.controlFillHovered;
    m_tokens.controlFillPressed = colors.controlFillPressed;
    m_tokens.separator = colors.separator;
    m_tokens.border = colors.border;
    m_tokens.borderStrong = colors.borderStrong;
    m_tokens.primaryText = colors.textPrimary;
    m_tokens.secondaryText = colors.textSecondary;
    m_tokens.tertiaryText = colors.textTertiary;
    m_tokens.disabledText = colors.textDisabled;
    m_tokens.accent = colors.accent;
    m_tokens.accentHovered = colors.accentHovered;
    m_tokens.selection = withAlpha(colors.accent, isDark() ? 82 : 48);
    m_tokens.dropTarget = colors.accent;
    m_tokens.tierRowBackground = colors.elevatedBackground;
    m_tokens.imageBorder = colors.border;
    m_tokens.destructive = colors.destructive;
    m_tokens.warning = colors.warning;
    m_tokens.success = colors.success;
    m_tokens.shadow = colors.shadow;
    m_tokens.symbolPrimary = colors.symbolPrimary;
    m_tokens.symbolSecondary = colors.symbolSecondary;
    m_tokens.symbolDisabled = colors.symbolDisabled;
}

const ThemeTokens& activeThemeTokens() {
    static ThemeTokens tokens;
    static quint64 cachedGeneration = std::numeric_limits<quint64>::max();
    const vkui::VkTheme& vkTheme = vkui::VkThemeManager::instance()->theme();
    if (cachedGeneration != vkTheme.generation()) {
        tokens = Theme(vkTheme).tokens();
        cachedGeneration = vkTheme.generation();
    }
    return tokens;
}

bool activeThemeIsDark() {
    return vkui::VkThemeManager::instance()->effectiveAppearance() == vkui::VkAppearance::Dark;
}

QColor withAlpha(QColor color, int alpha) {
    color.setAlpha(qBound(0, alpha, 255));
    return color;
}

QColor withAlphaF(QColor color, qreal alpha) {
    color.setAlphaF(static_cast<float>(qBound<qreal>(0.0, alpha, 1.0)));
    return color;
}

QColor contrastingTextColor(const QColor& background) {
    const auto linearChannel = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const qreal luminance = 0.2126 * linearChannel(background.redF()) +
                            0.7152 * linearChannel(background.greenF()) +
                            0.0722 * linearChannel(background.blueF());
    const qreal whiteContrast = 1.05 / (luminance + 0.05);
    const qreal blackContrast = (luminance + 0.05) / 0.05;
    return whiteContrast >= blackContrast ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace tlm
