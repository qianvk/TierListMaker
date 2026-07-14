#include "navigation/SidebarDelegate.h"

#include "navigation/SidebarModel.h"
#include "navigation/SidebarView.h"
#include "theme/Theme.h"

#include <QPainter>
#include <vkui/core/VkIcon.h>

namespace tlm {

SidebarDelegate::SidebarDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void SidebarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const auto* view =
        qobject_cast<const SidebarView*>(option.widget ? option.widget->parentWidget() : nullptr);
    const int availableWidth = view ? view->availableWidth() : option.rect.width();
    QRect availableRect = option.rect;
    availableRect.setWidth(qMin(option.rect.width(), qMax(0, availableWidth)));
    const QRect r = availableRect.adjusted(8, 3, -8, -3);
    if (!r.isValid()) {
        painter->restore();
        return;
    }
    constexpr int kCompactSidebarWidth = 132;
    const bool compact = availableWidth < kCompactSidebarWidth;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool enabled = option.state.testFlag(QStyle::State_Enabled);
    const bool hovered = enabled && option.state.testFlag(QStyle::State_MouseOver);
    const bool dirty = index.data(SidebarModel::DirtyRole).toBool();
    const ThemeTokens& colors = activeThemeTokens();

    if (selected || hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? colors.selection : colors.controlFillHovered);
        painter->drawRoundedRect(r, 8, 8);
    }

    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    const QRect iconRect(compact ? r.center().x() - 9 : r.left() + 10, r.center().y() - 9, 18, 18);
    const QIcon::Mode iconMode =
        enabled ? (hovered ? QIcon::Active : QIcon::Normal) : QIcon::Disabled;
    icon.paint(painter, iconRect, Qt::AlignCenter, iconMode, selected ? QIcon::On : QIcon::Off);

    if (!compact) {
        painter->setPen(enabled ? colors.primaryText : colors.disabledText);
        const QRect textRect = r.adjusted(40, 0, dirty ? -30 : -8, 0);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(),
                                                            Qt::ElideRight, textRect.width()));
    }
    if (dirty) {
        const QRect dirtyRect = compact ? QRect(iconRect.right() - 4, iconRect.top() - 3, 10, 10)
                                        : QRect(r.right() - 23, r.center().y() - 7, 14, 14);
        vkui::icon(vkui::VkSymbol::UnsavedIndicator, vkui::VkIconRole::Accent)
            .paint(painter, dirtyRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
    }
    painter->restore();
}

QSize SidebarDelegate::sizeHint(const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    const QString text = index.data(Qt::DisplayRole).toString();
    const int textWidth = option.fontMetrics.horizontalAdvance(text);
    return QSize(qMax(160, textWidth + 40 + 30 + 28), 38);
}

} // namespace tlm
