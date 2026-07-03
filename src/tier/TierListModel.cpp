#include "tier/TierListModel.h"

#include <utility>

namespace tlm {

TierListModel::TierListModel(QObject* parent) : QAbstractListModel(parent) {}

void TierListModel::setProject(const TierProject* project) {
    beginResetModel();
    m_project = project;
    const int rows = m_project ? static_cast<int>(m_project->rows.size()) : 0;
    m_rowHeights = QVector<int>(rows, 1);
    m_rowUnits = QVector<int>(rows, 1);
    endResetModel();
}

int TierListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_project) {
        return 0;
    }
    return static_cast<int>(m_project->rows.size());
}

QVariant TierListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_project || index.row() < 0 ||
        index.row() >= static_cast<int>(m_project->rows.size())) {
        return {};
    }

    const TierRow& row = m_project->rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return row.label;
    case RowIdRole:
        return row.id;
    case ColorRole:
        return row.color;
    case OrderRole:
        return row.order;
    case ImageIdsRole:
        return row.imageIds;
    case RowHeightRole:
        return rowHeightAt(index.row());
    case RowUnitCountRole:
        return rowUnitCountAt(index.row());
    case FirstRowRole:
        return index.row() == 0;
    case LastRowRole:
        return index.row() == static_cast<int>(m_project->rows.size()) - 1;
    case Qt::SizeHintRole:
        return QSize(1, rowHeightAt(index.row()));
    default:
        return {};
    }
}

Qt::ItemFlags TierListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QString TierListModel::rowIdAt(int row) const {
    if (!m_project || row < 0 || row >= static_cast<int>(m_project->rows.size())) {
        return {};
    }
    return m_project->rows.at(row).id;
}

int TierListModel::rowForId(const QString& rowId) const {
    if (!m_project) {
        return -1;
    }
    for (int row = 0; row < static_cast<int>(m_project->rows.size()); ++row) {
        if (m_project->rows.at(row).id == rowId) {
            return row;
        }
    }
    return -1;
}

const TierRow* TierListModel::tierRowAt(int row) const {
    if (!m_project || row < 0 || row >= static_cast<int>(m_project->rows.size())) {
        return nullptr;
    }
    return &m_project->rows.at(row);
}

void TierListModel::setLayoutMetrics(QVector<int> rowHeights, QVector<int> rowUnits) {
    const int rows = rowCount();
    if (rowHeights.size() != rows || rowUnits.size() != rows) {
        return;
    }
    if (m_rowHeights == rowHeights && m_rowUnits == rowUnits) {
        return;
    }
    m_rowHeights = std::move(rowHeights);
    m_rowUnits = std::move(rowUnits);
    if (rows > 0) {
        emit dataChanged(index(0, 0), index(rows - 1, 0),
                         {Qt::SizeHintRole, RowHeightRole, RowUnitCountRole});
    }
}

int TierListModel::rowHeightAt(int row) const {
    if (row < 0 || row >= m_rowHeights.size()) {
        return 1;
    }
    return qMax(1, m_rowHeights.at(row));
}

int TierListModel::rowUnitCountAt(int row) const {
    if (row < 0 || row >= m_rowUnits.size()) {
        return 1;
    }
    return qMax(1, m_rowUnits.at(row));
}

} // namespace tlm
