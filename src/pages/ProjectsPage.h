#pragma once

#include "persistence/RecentProjectsStore.h"

#include <QWidget>

class QListView;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace tlm {

class ProjectRepository;
class RecentProjectsModel;

/** Recent-project browser with search, sorting, missing markers, and file actions. */
class ProjectsPage : public QWidget {
    Q_OBJECT

public:
    ProjectsPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                 QWidget* parent = nullptr);

public slots:
    void refresh();
    void focusSearch();
    void retranslateUi();

signals:
    void openProjectRequested(const QString& filePath);

private:
    QString selectedPath() const;
    void chooseCoverForSelectedProject();

    ProjectRepository* m_repository{nullptr};
    RecentProjectsStore* m_recentProjects{nullptr};
    QLineEdit* m_search{nullptr};
    QComboBox* m_sort{nullptr};
    QListView* m_view{nullptr};
    RecentProjectsModel* m_model{nullptr};
    QPushButton* m_openButton{nullptr};
    QPushButton* m_renameButton{nullptr};
    QPushButton* m_coverButton{nullptr};
    QPushButton* m_revealButton{nullptr};
    QPushButton* m_duplicateButton{nullptr};
    QPushButton* m_removeButton{nullptr};
    QPushButton* m_deleteFileButton{nullptr};
};

} // namespace tlm
