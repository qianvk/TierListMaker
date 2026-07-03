#pragma once

#include <QColor>

namespace tlm {

/** Semantic colors used by widgets so raw palettes remain centralized. */
struct ThemeTokens {
    QColor windowBackground;
    QColor sidebarBackground;
    QColor contentBackground;
    QColor separator;
    QColor primaryText;
    QColor secondaryText;
    QColor accent;
    QColor selection;
    QColor tierRowBackground;
};

} // namespace tlm

