#pragma once

#include <QStyledItemDelegate>

namespace tlm {

/** Custom delegate that paints rounded macOS-like sidebar selection pills. */
class SidebarDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit SidebarDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace tlm

