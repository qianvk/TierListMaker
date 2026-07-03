#include "widgets/RoundedButton.h"

namespace tlm {

RoundedButton::RoundedButton(QWidget* parent) : QPushButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(32);
    setStyleSheet(QStringLiteral(
        "QPushButton{border:1px solid palette(mid);border-radius:8px;padding:6px 12px;"
        "background:palette(alternate-base);color:palette(window-text);}"
        "QPushButton:hover{background:palette(midlight);}"
        "QPushButton:pressed{background:palette(midlight);}"
        "QPushButton:disabled{color:palette(mid);background:transparent;}"));
}

RoundedButton::RoundedButton(const QString& text, QWidget* parent) : RoundedButton(parent) {
    setText(text);
}

} // namespace tlm
