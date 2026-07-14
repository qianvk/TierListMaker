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
    case Qt::ToolTipRole:
        return item.title;
    case DirtyRole:
        return item.dirty;
    case PageRole:
        return static_cast<int>(item.page);
    default:
        return {};
    }
}

Qt::ItemFlags SidebarModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags result = QAbstractListModel::flags(index);
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return result;
    }
    if (!m_items[index.row()].enabled) {
        result &= ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    return result;
}

AppPage SidebarModel::pageForIndex(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return AppPage::Projects;
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

void SidebarModel::setPageEnabled(AppPage page, bool enabled) {
    const int row = rowForPage(page);
    if (row < 0 || m_items[row].enabled == enabled) {
        return;
    }
    m_items[row].enabled = enabled;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {Qt::DisplayRole});
}

void SidebarModel::setPageDirty(AppPage page, bool dirty) {
    const int row = rowForPage(page);
    if (row < 0 || m_items[row].dirty == dirty) {
        return;
    }
    m_items[row].dirty = dirty;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {DirtyRole});
}

void SidebarModel::retranslate() {
    const int editRow = rowForPage(AppPage::Edit);
    const bool editEnabled = editRow < 0 ? true : m_items[editRow].enabled;
    const bool editDirty = editRow < 0 ? false : m_items[editRow].dirty;
    beginResetModel();
    m_items = {{AppPage::Edit, tr("Edit"), vkui::icon(vkui::VkSymbol::Edit)},
               {AppPage::Projects, tr("Projects"), vkui::icon(vkui::VkSymbol::Projects)}};
    if (!m_items.isEmpty()) {
        m_items[0].enabled = editEnabled;
        m_items[0].dirty = editDirty;
    }
    endResetModel();
}

int SidebarModel::rowForPage(AppPage page) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].page == page) {
            return i;
        }
    }
    return -1;
}

} // namespace tlm
