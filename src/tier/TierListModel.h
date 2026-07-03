#pragma once

#include "tier/TierProject.h"

#include <QAbstractListModel>
#include <QSize>
#include <QVector>

namespace tlm {

/** Read-only list model exposing persisted tier rows to the tier list view. */
class TierListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        RowIdRole = Qt::UserRole + 1,
        LabelRole,
        ColorRole,
        OrderRole,
        ImageIdsRole,
        RowHeightRole,
        RowUnitCountRole,
        FirstRowRole,
        LastRowRole
    };

    explicit TierListModel(QObject* parent = nullptr);

    void setProject(const TierProject* project);
    const TierProject* project() const { return m_project; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QString rowIdAt(int row) const;
    int rowForId(const QString& rowId) const;
    const TierRow* tierRowAt(int row) const;

    void setLayoutMetrics(QVector<int> rowHeights, QVector<int> rowUnits);
    int rowHeightAt(int row) const;
    int rowUnitCountAt(int row) const;

private:
    const TierProject* m_project{nullptr};
    QVector<int> m_rowHeights;
    QVector<int> m_rowUnits;
};

} // namespace tlm
