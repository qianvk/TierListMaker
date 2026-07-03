#pragma once

#include "persistence/ProjectSerializer.h"

#include <QObject>
#include <QString>

namespace tlm {

/** Opens and saves `.tlmproject` files using ProjectSerializer and atomic writes. */
class ProjectRepository : public QObject {
    Q_OBJECT

public:
    explicit ProjectRepository(QObject* parent = nullptr);

    Result<TierProject> openProject(const QString& filePath) const;
    Result<void> saveProject(TierProject& project, const QString& filePath) const;

private:
    ProjectSerializer m_serializer;
};

} // namespace tlm

