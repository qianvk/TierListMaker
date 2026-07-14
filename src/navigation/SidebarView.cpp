#include "navigation/SidebarView.h"

#include "navigation/SidebarDelegate.h"

#include <QAbstractItemDelegate>
#include <QStyleOptionViewItem>

namespace tlm {

SidebarView::SidebarView(QWidget* parent) : QListView(parent) {
    setItemDelegate(new SidebarDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    setUniformItemSizes(true);
    setStyleSheet(QStringLiteral("QListView{background:transparent;outline:0;}"));
}

int SidebarView::preferredWidth() const {
    const QAbstractItemDelegate* delegate = itemDelegate();
    const QAbstractItemModel* sourceModel = model();
    if (!delegate || !sourceModel) {
        return 208;
    }
    int width = 208;
    QStyleOptionViewItem option;
    option.initFrom(this);
    for (int row = 0; row < sourceModel->rowCount(rootIndex()); ++row) {
        const QModelIndex index = sourceModel->index(row, 0, rootIndex());
        width = qMax(width, delegate->sizeHint(option, index).width());
    }
    return width;
}

void SidebarView::setAvailableWidth(int width) {
    width = qMax(0, width);
    if (m_availableWidth == width) {
        return;
    }
    m_availableWidth = width;
    if (viewport()) {
        viewport()->update();
    }
}

} // namespace tlm
