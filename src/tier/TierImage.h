#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

#include <optional>

namespace tlm {

/** Persisted image metadata for an imported item. Large pixmaps are cached outside this class. */
class TierImage {
public:
    QString id;
    QString sourcePath;
    QString importedAssetPath;
    QString originalFileName;
    QString displayName;
    int width{0};
    int height{0};
    QString thumbnailPath;
    std::optional<QString> assignedTierRowId;
    int order{0};
    QRectF cropRect;

    QSize size() const { return QSize(width, height); }
    bool isAssigned() const { return assignedTierRowId.has_value(); }
    bool hasCropRect() const;
    QRect thumbnailSourceRect(const QSize& sourceSize, const QSize& targetSize) const;
};

} // namespace tlm
