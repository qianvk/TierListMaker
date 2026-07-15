#include "window/SidebarToggleButton.h"

#include <vkui/core/VkIcon.h>

namespace tlm {

SidebarToggleButton::SidebarToggleButton(QWidget* parent) : QToolButton(parent) {
    setToolTip(tr("Toggle sidebar"));
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setIcon(vkui::icon(vkui::VkSymbol::Sidebar));
    setIconSize(QSize(20, 20));
    setAutoRaise(true);
}

} // namespace tlm
