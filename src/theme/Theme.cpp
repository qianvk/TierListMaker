#include "theme/Theme.h"

namespace tlm {

Theme::Theme(Kind kind) : m_kind(kind) {
    if (kind == Kind::Dark) {
        m_tokens.windowBackground = QColor(QStringLiteral("#16161e"));
        m_tokens.sidebarBackground = QColor(QStringLiteral("#1a1b26"));
        m_tokens.contentBackground = QColor(QStringLiteral("#1a1b26"));
        m_tokens.separator = QColor(QStringLiteral("#292e42"));
        m_tokens.primaryText = QColor(QStringLiteral("#c0caf5"));
        m_tokens.secondaryText = QColor(QStringLiteral("#9aa5ce"));
        m_tokens.accent = QColor(QStringLiteral("#7aa2f7"));
        m_tokens.selection = QColor(QStringLiteral("#283457"));
        m_tokens.tierRowBackground = QColor(QStringLiteral("#24283b"));
    } else {
        m_tokens.windowBackground = QColor(QStringLiteral("#f5f6f8"));
        m_tokens.sidebarBackground = QColor(QStringLiteral("#eceff3"));
        m_tokens.contentBackground = QColor(QStringLiteral("#ffffff"));
        m_tokens.separator = QColor(QStringLiteral("#d8dce2"));
        m_tokens.primaryText = QColor(QStringLiteral("#1f2328"));
        m_tokens.secondaryText = QColor(QStringLiteral("#69707a"));
        m_tokens.accent = QColor(QStringLiteral("#2f80ed"));
        m_tokens.selection = QColor(QStringLiteral("#dcecff"));
        m_tokens.tierRowBackground = QColor(QStringLiteral("#f7f8fa"));
    }
}

} // namespace tlm
