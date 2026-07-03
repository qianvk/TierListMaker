#pragma once

#include "settings/AppSettings.h"
#include "theme/Theme.h"

#include <QObject>

class QApplication;

namespace tlm {

/** Owns the active theme and applies platform-aware palettes to the Qt application. */
class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(AppSettings* settings, QObject* parent = nullptr);

    const Theme& theme() const { return m_theme; }
    const ThemeTokens& tokens() const { return m_theme.tokens(); }
    void applyTo(QApplication& app);

signals:
    void themeChanged(const tlm::Theme& theme);

private:
    Theme::Kind resolveKind() const;

    AppSettings* m_settings{nullptr};
    Theme m_theme;
};

} // namespace tlm

