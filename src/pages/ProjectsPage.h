#pragma once

#include "persistence/RecentProjectsStore.h"

#include <QWidget>

class QModelIndex;
class QPoint;
class QListView;
class QComboBox;
class QLineEdit;

namespace tlm {

class AppSettings;
class ProjectRepository;
class RecentProjectsModel;

/** Recent-project browser with search, sorting, missing markers, and file actions. */
class ProjectsPage : public QWidget {
    Q_OBJECT

public:
    ProjectsPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                 AppSettings* settings,
                 QWidget* parent = nullptr);

public slots:
    void refresh();
    void focusSearch();
    void openProjectFromDialog();
    void retranslateUi();

signals:
    void openProjectRequested(const QString& filePath);

private:
    QString selectedPath() const;
    RecentProjectEntry selectedEntry() const;
    void showProjectContextMenu(const QPoint& point);
    void openSelectedProject();
    void renameSelectedProject();
    void chooseCoverForSelectedProject();
    void revealSelectedProject();
    void saveSelectedProjectAs();
    void deleteSelectedProject();

    ProjectRepository* m_repository{nullptr};
    RecentProjectsStore* m_recentProjects{nullptr};
    AppSettings* m_settings{nullptr};
    QLineEdit* m_search{nullptr};
    QComboBox* m_sort{nullptr};
    QListView* m_view{nullptr};
    RecentProjectsModel* m_model{nullptr};
};

} // namespace tlm
