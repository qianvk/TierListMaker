#pragma once

#include <QAbstractButton>

namespace tlm {

/** macOS-style sidebar toggle button used as a top-chrome overlay. */
class SidebarToggleButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit SidebarToggleButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace tlm
