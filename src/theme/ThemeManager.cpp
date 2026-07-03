#include "theme/ThemeManager.h"

#include <QApplication>
#include <QPalette>
#include <QString>
#include <QStyleHints>

namespace tlm {

namespace {
QString buildDarkColorStyleSheet(const ThemeTokens& t) {
    const QString bg = t.contentBackground.name(QColor::HexRgb);
    const QString panel = t.tierRowBackground.name(QColor::HexRgb);
    const QString text = t.primaryText.name(QColor::HexRgb);
    const QString muted = t.secondaryText.name(QColor::HexRgb);
    const QString selection = t.selection.name(QColor::HexRgb);

    return QStringLiteral(R"(
QWidget {
    color:%1;
    selection-background-color:%2;
    selection-color:#ffffff;
}
QFrame#Content, QStackedWidget#Pages {
    background:%3;
}
QDialog, QMessageBox, QFileDialog {
    background:%3;
    color:%1;
}
QToolTip {
    background:transparent;
    color:%1;
}
QAbstractButton,
QLineEdit,
QTextEdit,
QPlainTextEdit,
QComboBox,
QSpinBox,
QDoubleSpinBox {
    color:%1;
}
QPushButton:disabled, QToolButton:disabled {
    color:%4;
}
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color:%4;
}
QAbstractItemView {
    color:%1;
    background:%5;
    selection-background-color:%2;
    selection-color:#ffffff;
}
QListView, QListWidget, QTreeView, QTableView {
    background:transparent;
    alternate-background-color:%5;
}
QMenu {
    color:%1;
    background:%5;
}
QMenu::item:selected {
    background:%2;
}
QLabel:disabled {
    color:%4;
}
)").arg(text, selection, bg, muted, panel);
}
} // namespace

ThemeManager::ThemeManager(AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings), m_theme(resolveKind()) {
    connect(m_settings, &AppSettings::appearanceChanged, this, [this](AppearanceMode) {
        m_theme = Theme(resolveKind());
        if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
            applyTo(*app);
        }
        emit themeChanged(m_theme);
    });
    if (qApp && qApp->styleHints()) {
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (!m_settings || m_settings->appearance() != AppearanceMode::System) {
                return;
            }
            m_theme = Theme(resolveKind());
            if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
                applyTo(*app);
            }
            emit themeChanged(m_theme);
        });
    }
}

void ThemeManager::applyTo(QApplication& app) {
    const ThemeTokens& t = m_theme.tokens();
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, t.windowBackground);
    palette.setColor(QPalette::Base, t.contentBackground);
    palette.setColor(QPalette::AlternateBase, t.tierRowBackground);
    palette.setColor(QPalette::Text, t.primaryText);
    palette.setColor(QPalette::WindowText, t.primaryText);
    palette.setColor(QPalette::ButtonText, t.primaryText);
    palette.setColor(QPalette::PlaceholderText, t.secondaryText);
    palette.setColor(QPalette::Highlight, t.accent);
    palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    palette.setColor(QPalette::Button, t.tierRowBackground);
    palette.setColor(QPalette::Mid, t.separator);
    palette.setColor(QPalette::Midlight, t.selection);
    palette.setColor(QPalette::Disabled, QPalette::Text, t.secondaryText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, t.secondaryText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, t.secondaryText);
    app.setPalette(palette);
    // Keep light mode visually identical to the app-specific styles. Dark mode only
    // supplies color roles here; size, padding, borders, and icon metrics stay local.
    app.setStyleSheet(m_theme.isDark() ? buildDarkColorStyleSheet(t) : QString());
}

Theme::Kind ThemeManager::resolveKind() const {
    if (!m_settings) {
        return Theme::Kind::Light;
    }
    switch (m_settings->appearance()) {
    case AppearanceMode::Dark:
        return Theme::Kind::Dark;
    case AppearanceMode::Light:
        return Theme::Kind::Light;
    case AppearanceMode::System:
    default:
        if (qApp && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
            return Theme::Kind::Dark;
        }
        return Theme::Kind::Light;
    }
}

} // namespace tlm
