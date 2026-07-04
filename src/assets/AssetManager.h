#pragma once

#include "persistence/Result.h"
#include "settings/AppSettings.h"
#include "tier/TierProject.h"

#include <QObject>
#include <QTemporaryDir>

namespace tlm {

/** Imports images, manages local asset paths, and migrates unsaved session assets on save. */
class AssetManager : public QObject {
    Q_OBJECT

public:
    explicit AssetManager(QObject* parent = nullptr);

    QStringList supportedNameFilters() const;
    Result<QStringList> importImages(TierProject& project, const QStringList& sourcePaths,
                                     ImageImportBehavior behavior);
    Result<QString> importCanvasImage(TierProject& project, const QString& sourcePath,
                                      const QString& canvasKey, ImageImportBehavior behavior);
    Result<bool> migrateSessionAssets(TierProject& project, const QString& targetProjectPath);
    QString resolvedImagePath(const TierProject& project, const TierImage& image) const;
    QString assetsDirectoryForProjectPath(const QString& projectPath) const;
    QString sessionDirectory() const;

private:
    Result<TierImage> importOne(TierProject& project, const QString& sourcePath,
                                ImageImportBehavior behavior, int order);
    QString uniqueAssetName(const QString& sourcePath) const;
    Result<QString> copyAsset(const QString& sourcePath, const QString& destinationDir) const;
    bool isSessionAsset(const QString& path) const;

    QTemporaryDir m_sessionDir;
};

} // namespace tlm
