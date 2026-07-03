#include "persistence/ProjectRepository.h"

#include "persistence/AtomicFileWriter.h"

#include <QFile>
#include <QFileInfo>

namespace tlm {

ProjectRepository::ProjectRepository(QObject* parent) : QObject(parent) {}

Result<TierProject> ProjectRepository::openProject(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<TierProject>::failure(tr("Could not open the project file."), file.errorString());
    }
    auto result = m_serializer.deserialize(file.readAll(), QFileInfo(filePath).absoluteFilePath());
    if (!result) {
        return result;
    }
    return Result<TierProject>::success(result.takeValue());
}

Result<void> ProjectRepository::saveProject(TierProject& project, const QString& filePath) const {
    auto serialized = m_serializer.serialize(project);
    if (!serialized) {
        return Result<void>::failure(serialized.error().message, serialized.error().details);
    }
    const QFileInfo targetInfo(filePath);
    if (targetInfo.exists()) {
        QFile existing(filePath);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == serialized.value()) {
            project.filePath = targetInfo.absoluteFilePath();
            project.dirty = false;
            return Result<void>::success();
        }
    }
    auto writeResult = AtomicFileWriter::write(filePath, serialized.value());
    if (!writeResult) {
        return writeResult;
    }
    project.filePath = targetInfo.absoluteFilePath();
    project.dirty = false;
    return Result<void>::success();
}

} // namespace tlm
