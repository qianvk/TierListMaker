#pragma once

#include "assets/AssetManager.h"
#include "export/ExportOptions.h"
#include "persistence/Result.h"
#include "tier/TierProject.h"

#include <QObject>

namespace tlm {

/** Renders complete tier lists to PNG, JPEG, or PDF without mutating the project. */
class TierListExporter : public QObject {
    Q_OBJECT

public:
    explicit TierListExporter(const AssetManager* assetManager, QObject* parent = nullptr);

    QImage renderToImage(const TierProject& project, const ExportOptions& options) const;
    Result<void> exportProject(const TierProject& project, const QString& filePath,
                               const ExportOptions& options) const;

private:
    const AssetManager* m_assetManager{nullptr};
};

} // namespace tlm

