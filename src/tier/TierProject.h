#pragma once

#include "tier/TierImage.h"
#include "tier/TierRow.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace tlm {

/** In-memory domain model for a complete tier-list project. */
class TierProject {
public:
    QString id;
    QString name;
    QString filePath;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString thumbnailPath;
    QJsonObject canvas;
    QVector<TierRow> rows;
    QVector<TierImage> images;
    QJsonObject settings;
    bool dirty{false};

    static TierProject createUntitled();

    TierRow* rowById(const QString& rowId);
    const TierRow* rowById(const QString& rowId) const;
    TierImage* imageById(const QString& imageId);
    const TierImage* imageById(const QString& imageId) const;

    QVector<TierImage*> unassignedImages();
    QVector<const TierImage*> unassignedImages() const;
    QVector<const TierImage*> imagesForRow(const QString& rowId) const;

    void resetDefaultRows();
    void normalizeOrdering();
    void touch();
    QString suggestedFileName() const;
};

} // namespace tlm
