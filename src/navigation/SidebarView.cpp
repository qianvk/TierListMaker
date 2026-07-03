#include "navigation/SidebarView.h"

#include "navigation/SidebarDelegate.h"

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

} // namespace tlm

