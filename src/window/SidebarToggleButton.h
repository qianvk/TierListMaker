#pragma once

#include <QToolButton>

namespace tlm {

/** macOS-style sidebar toggle button used as a top-chrome overlay. */
class SidebarToggleButton : public QToolButton {
    Q_OBJECT

public:
    explicit SidebarToggleButton(QWidget* parent = nullptr);
};

} // namespace tlm
