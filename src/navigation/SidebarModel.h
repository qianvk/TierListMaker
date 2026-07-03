#pragma once

#include <QAbstractListModel>
#include <QIcon>

namespace tlm {

enum class AppPage { Edit = 0, Projects = 1, Preferences = 2 };

/** List model for primary left-sidebar navigation. */
class SidebarModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { PageRole = Qt::UserRole + 1, IconRole };

    explicit SidebarModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    AppPage pageForIndex(const QModelIndex& index) const;
    QModelIndex indexForPage(AppPage page) const;
    void retranslate();

private:
    struct Item {
        AppPage page;
        QString title;
        QIcon icon;
    };
    QVector<Item> m_items;
};

} // namespace tlm

