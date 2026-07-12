#include "navigation/SidebarModel.h"

#include <vkui/core/VkIcon.h>

namespace tlm {

SidebarModel::SidebarModel(QObject* parent) : QAbstractListModel(parent) {
    retranslate();
}

int SidebarModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant SidebarModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const Item& item = m_items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return item.title;
    case Qt::DecorationRole:
    case IconRole:
        return item.icon;
    case PageRole:
        return static_cast<int>(item.page);
    default:
        return {};
    }
}

AppPage SidebarModel::pageForIndex(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return AppPage::Edit;
    }
    return m_items[index.row()].page;
}

QModelIndex SidebarModel::indexForPage(AppPage page) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].page == page) {
            return index(i, 0);
        }
    }
    return {};
}

void SidebarModel::retranslate() {
    beginResetModel();
    m_items = {{AppPage::Edit, tr("Edit"), vkui::icon(vkui::VkSymbol::Edit)},
               {AppPage::Projects, tr("Projects"), vkui::icon(vkui::VkSymbol::Projects)}};
    endResetModel();
}

} // namespace tlm
