#include "window/SidebarToggleButton.h"

#include <vkui/core/VkIcon.h>

namespace tlm {

SidebarToggleButton::SidebarToggleButton(QWidget* parent) : QToolButton(parent) {
    setToolTip(tr("Toggle sidebar"));
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    setCursor(Qt::ArrowCursor);
#else
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
#endif
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setIcon(vkui::icon(vkui::VkSymbol::Sidebar));
    setIconSize(QSize(20, 20));
    setAutoRaise(true);
}

} // namespace tlm
