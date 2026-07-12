#pragma once

#include "theme/ThemeTokens.h"

#include <vkui/core/VkTheme.h>

namespace tlm {

/** Immutable application-specific projection of the active VkUI theme. */
class Theme {
public:
    enum class Kind { Light, Dark };

    Theme();
    explicit Theme(const vkui::VkTheme& theme);

    Kind kind() const {
        return m_kind;
    }
    const ThemeTokens& tokens() const {
        return m_tokens;
    }
    bool isDark() const {
        return m_kind == Kind::Dark;
    }

private:
    Kind m_kind;
    ThemeTokens m_tokens;
};

/** Returns cached application tokens for the current VkUI theme generation. */
const ThemeTokens& activeThemeTokens();
bool activeThemeIsDark();
QColor withAlpha(QColor color, int alpha);
QColor withAlphaF(QColor color, qreal alpha);
QColor contrastingTextColor(const QColor& background);

} // namespace tlm
