#pragma once

#include <QPushButton>

namespace tlm {

/** Rounded icon/text push button with consistent app styling hooks. */
class RoundedButton : public QPushButton {
    Q_OBJECT

public:
    explicit RoundedButton(QWidget* parent = nullptr);
    explicit RoundedButton(const QString& text, QWidget* parent = nullptr);
};

} // namespace tlm

