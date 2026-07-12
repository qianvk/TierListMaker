#pragma once

#include <QColor>

namespace tlm {

/** Semantic colors used by widgets so raw palettes remain centralized. */
struct ThemeTokens {
    QColor windowBackground;
    QColor sidebarBackground;
    QColor contentBackground;
    QColor elevatedBackground;
    QColor popoverBackground;
    QColor controlFill;
    QColor controlFillHovered;
    QColor controlFillPressed;
    QColor separator;
    QColor border;
    QColor borderStrong;
    QColor primaryText;
    QColor secondaryText;
    QColor tertiaryText;
    QColor disabledText;
    QColor accent;
    QColor accentHovered;
    QColor selection;
    QColor dropTarget;
    QColor tierRowBackground;
    QColor imageBorder;
    QColor destructive;
    QColor warning;
    QColor success;
    QColor shadow;
    QColor symbolPrimary;
    QColor symbolSecondary;
    QColor symbolDisabled;
};

} // namespace tlm
