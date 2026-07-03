#include "window/SidebarToggleButton.h"

#include <QIcon>
#include <QPainter>
#include <QPaintEvent>

namespace tlm {

SidebarToggleButton::SidebarToggleButton(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setIcon(QIcon(QStringLiteral(":/icons/sidebar.svg")));
    setIconSize(QSize(20, 20));
}

void SidebarToggleButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(isDown() ? 0.55 : (underMouse() ? 0.82 : 1.0));

    const QSize size = iconSize();
    const QRect iconRect((width() - size.width()) / 2, (height() - size.height()) / 2,
                         size.width(), size.height());
    icon().paint(&painter, iconRect, Qt::AlignCenter,
                 isEnabled() ? QIcon::Normal : QIcon::Disabled,
                 isChecked() ? QIcon::On : QIcon::Off);
}

} // namespace tlm
