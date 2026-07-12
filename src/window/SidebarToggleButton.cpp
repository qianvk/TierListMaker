#include "window/SidebarToggleButton.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

namespace tlm {

SidebarToggleButton::SidebarToggleButton(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setToolTip(tr("Toggle sidebar"));
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    setCursor(Qt::ArrowCursor);
#else
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
#endif
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setIcon(QIcon(QStringLiteral(":/icons/sidebar.svg")));
    setIconSize(QSize(20, 20));
}

bool SidebarToggleButton::event(QEvent* event) {
    const bool result = QAbstractButton::event(event);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::EnabledChange:
        updateHoverEffect();
        update();
        break;
    default:
        break;
    }
#endif
    return result;
}

void SidebarToggleButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    QColor fill;
    if (isDown()) {
        fill = QColor(60, 80, 110, 48);
    } else if (underMouse()) {
        fill = QColor(60, 80, 110, 28);
    }
    if (fill.isValid()) {
        const QRectF background = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QPainterPath path;
        path.addRoundedRect(background, 8.0, 8.0);
        painter.fillPath(path, fill);
    }
#endif

    painter.setOpacity(isDown() ? 0.55 : (underMouse() ? 0.82 : 1.0));

    const QSize size = iconSize();
    const QRect iconRect((width() - size.width()) / 2, (height() - size.height()) / 2,
                         size.width(), size.height());
    icon().paint(&painter, iconRect, Qt::AlignCenter,
                 isEnabled() ? QIcon::Normal : QIcon::Disabled,
                 isChecked() ? QIcon::On : QIcon::Off);
}

void SidebarToggleButton::updateHoverEffect() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return;
#else
    if (!isEnabled() || (!underMouse() && !isDown())) {
        setGraphicsEffect(nullptr);
        return;
    }

    auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect());
    if (!shadow) {
        shadow = new QGraphicsDropShadowEffect(this);
        shadow->setOffset(0, 2);
        setGraphicsEffect(shadow);
    }
    shadow->setBlurRadius(isDown() ? 10 : 14);
    shadow->setColor(QColor(28, 35, 50, isDown() ? 58 : 72));
#endif
}

} // namespace tlm
