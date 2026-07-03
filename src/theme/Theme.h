#pragma once

#include "theme/ThemeTokens.h"

namespace tlm {

/** Immutable theme value object for light or dark appearance. */
class Theme {
public:
    enum class Kind { Light, Dark };

    explicit Theme(Kind kind = Kind::Light);

    Kind kind() const { return m_kind; }
    const ThemeTokens& tokens() const { return m_tokens; }
    bool isDark() const { return m_kind == Kind::Dark; }

private:
    Kind m_kind;
    ThemeTokens m_tokens;
};

} // namespace tlm

