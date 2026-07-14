#include "assets/AssetManager.h"

#include "assets/ImageLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QUuid>

namespace tlm {

AssetManager::AssetManager(QObject* parent) : QObject(parent) {
    m_sessionDir.setAutoRemove(true);
}

QStringList AssetManager::supportedNameFilters() const {
    return ImageLoader::supportedNameFilters();
}

Result<QStringList> AssetManager::importImages(TierProject& project, const QStringList& sourcePaths) {
    QStringList ids;
    int order = static_cast<int>(project.unassignedImages().size());
    for (const QString& sourcePath : sourcePaths) {
        auto result = importOne(project, sourcePath, order++);
        if (!result) {
            return Result<QStringList>::failure(result.error().message, result.error().details);
        }
        TierImage image = result.takeValue();
        ids.append(image.id);
        project.images.append(image);
    }
    if (!ids.isEmpty()) {
        project.touch();
    }
    return Result<QStringList>::success(ids);
}

Result<QString> AssetManager::importCanvasImage(TierProject& project, const QString& sourcePath,
                                                const QString& canvasKey) {
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return Result<QString>::failure(tr("The selected image does not exist."), sourcePath);
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return Result<QString>::failure(tr("Unsupported or unreadable image format."),
                                        reader.errorString());
    }

    const QString destinationDir =
        project.filePath.isEmpty() ? QDir(m_sessionDir.path()).filePath(QStringLiteral("assets"))
                                   : assetsDirectoryForProjectPath(project.filePath);
    auto copied = copyAsset(sourcePath, destinationDir);
    if (!copied) {
        return Result<QString>::failure(copied.error().message, copied.error().details);
    }
    const QString storedPath = project.filePath.isEmpty()
                                   ? copied.value()
                                   : QDir(QFileInfo(project.filePath).absolutePath())
                                         .relativeFilePath(copied.value());

    project.canvas.insert(canvasKey, storedPath);
    project.touch();
    return Result<QString>::success(storedPath);
}

Result<bool> AssetManager::migrateSessionAssets(TierProject& project, const QString& targetProjectPath) {
    const QString assetDir = assetsDirectoryForProjectPath(targetProjectPath);
    if (!QDir().mkpath(assetDir)) {
        return Result<bool>::failure(tr("Could not create the project assets folder."), assetDir);
    }
    const QDir projectDir(QFileInfo(targetProjectPath).absolutePath());
    bool changed = false;

    auto migratePath = [&](QString& path, const QString& failureMessage) -> Result<bool> {
        if (path.isEmpty() || !isSessionAsset(path)) {
            return Result<bool>::success(false);
        }
        const QString destination = QDir(assetDir).filePath(QFileInfo(path).fileName());
        if (QFile::exists(destination) && !QFile::remove(destination)) {
            return Result<bool>::failure(tr("Could not replace an existing project asset."), destination);
        }
        if (!QFile::copy(path, destination)) {
            return Result<bool>::failure(failureMessage, destination);
        }
        path = projectDir.relativeFilePath(destination);
        return Result<bool>::success(true);
    };

    for (TierImage& image : project.images) {
        auto migrated = migratePath(image.importedAssetPath, tr("Could not migrate an imported image."));
        if (!migrated) {
            return migrated;
        }
        changed = changed || migrated.value();
    }

    QString backgroundPath = project.canvas.value(QStringLiteral("backgroundImagePath")).toString();
    auto backgroundMigrated = migratePath(backgroundPath, tr("Could not migrate the tier-list background image."));
    if (!backgroundMigrated) {
        return backgroundMigrated;
    }
    if (backgroundMigrated.value()) {
        project.canvas.insert(QStringLiteral("backgroundImagePath"), backgroundPath);
        changed = true;
    }

    auto thumbnailMigrated = migratePath(project.thumbnailPath, tr("Could not migrate the project cover image."));
    if (!thumbnailMigrated) {
        return thumbnailMigrated;
    }
    changed = changed || thumbnailMigrated.value();

    if (!project.cover.isEmpty()) {
        QString coverSource = project.cover.value(QStringLiteral("sourceImagePath")).toString();
        auto coverSourceMigrated =
            migratePath(coverSource, tr("Could not migrate the project cover source image."));
        if (!coverSourceMigrated) {
            return coverSourceMigrated;
        }
        if (coverSourceMigrated.value()) {
            project.cover.insert(QStringLiteral("sourceImagePath"), coverSource);
            changed = true;
        }

        QString coverCropped = project.cover.value(QStringLiteral("croppedImagePath")).toString();
        auto coverCroppedMigrated =
            migratePath(coverCropped, tr("Could not migrate the project cover image."));
        if (!coverCroppedMigrated) {
            return coverCroppedMigrated;
        }
        if (coverCroppedMigrated.value()) {
            project.cover.insert(QStringLiteral("croppedImagePath"), coverCropped);
            changed = true;
        }
    }

    return Result<bool>::success(changed);
}

QString AssetManager::resolvedImagePath(const TierProject& project, const TierImage& image) const {
    if (!image.importedAssetPath.isEmpty()) {
        const QFileInfo imported(image.importedAssetPath);
        if (imported.isAbsolute()) {
            return imported.absoluteFilePath();
        }
        if (!project.filePath.isEmpty()) {
            return QDir(QFileInfo(project.filePath).absolutePath()).filePath(image.importedAssetPath);
        }
    }
    return image.sourcePath;
}

QString AssetManager::assetsDirectoryForProjectPath(const QString& projectPath) const {
    return QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral("assets"));
}

QString AssetManager::sessionDirectory() const {
    return m_sessionDir.path();
}

Result<TierImage> AssetManager::importOne(TierProject& project, const QString& sourcePath,
                                          int order) {
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return Result<TierImage>::failure(tr("The selected image does not exist."), sourcePath);
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QSize imageSize = reader.size();
    if (!reader.canRead()) {
        return Result<TierImage>::failure(tr("Unsupported or unreadable image format."),
                                          reader.errorString());
    }

    TierImage image;
    image.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    image.sourcePath = sourceInfo.absoluteFilePath();
    image.originalFileName = sourceInfo.fileName();
    image.displayName = sourceInfo.completeBaseName();
    image.width = imageSize.width();
    image.height = imageSize.height();
    image.order = order;

    const QString destinationDir =
        project.filePath.isEmpty() ? QDir(m_sessionDir.path()).filePath(QStringLiteral("assets"))
                                   : assetsDirectoryForProjectPath(project.filePath);
    auto copied = copyAsset(sourcePath, destinationDir);
    if (!copied) {
        return Result<TierImage>::failure(copied.error().message, copied.error().details);
    }
    if (project.filePath.isEmpty()) {
        image.importedAssetPath = copied.value();
    } else {
        image.importedAssetPath =
            QDir(QFileInfo(project.filePath).absolutePath()).relativeFilePath(copied.value());
    }

    return Result<TierImage>::success(image);
}

QString AssetManager::uniqueAssetName(const QString& sourcePath) const {
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    const QString extension = suffix.isEmpty() ? QStringLiteral("img") : suffix;
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".") + extension;
}

Result<QString> AssetManager::copyAsset(const QString& sourcePath, const QString& destinationDir) const {
    if (!QDir().mkpath(destinationDir)) {
        return Result<QString>::failure(tr("Could not create the image asset folder."), destinationDir);
    }
    const QString destination = QDir(destinationDir).filePath(uniqueAssetName(sourcePath));
    if (!QFile::copy(sourcePath, destination)) {
        return Result<QString>::failure(tr("Could not copy the imported image."), destination);
    }
    return Result<QString>::success(QFileInfo(destination).absoluteFilePath());
}

bool AssetManager::isSessionAsset(const QString& path) const {
    return QFileInfo(path).absoluteFilePath().startsWith(QFileInfo(m_sessionDir.path()).absoluteFilePath());
}

} // namespace tlm
