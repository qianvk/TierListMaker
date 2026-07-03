#include "navigation/SidebarDelegate.h"

#include <QPainter>

namespace tlm {

SidebarDelegate::SidebarDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void SidebarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect r = option.rect.adjusted(8, 3, -8, -3);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

    if (selected || hovered) {
        QColor fill = selected ? option.palette.highlight().color() : option.palette.midlight().color();
        fill.setAlpha(selected ? 70 : 35);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(r, 8, 8);
    }

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QRect iconRect(r.left() + 10, r.center().y() - 9, 18, 18);
    icon.paint(painter, iconRect);

    painter->setPen(option.palette.color(QPalette::WindowText));
    painter->drawText(r.adjusted(40, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
}

QSize SidebarDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return QSize(180, 38);
}

} // namespace tlm

