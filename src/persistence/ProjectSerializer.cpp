#include "persistence/ProjectSerializer.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <optional>

namespace tlm {

namespace {
QString requiredString(const QJsonObject& object, const QString& key, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        *error = QObject::tr("Expected string field '%1'.").arg(key);
        return {};
    }
    return value.toString();
}

int requiredInt(const QJsonObject& object, const QString& key, QString* error, int fallback = 0) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        *error = QObject::tr("Expected number field '%1'.").arg(key);
        return fallback;
    }
    return value.toInt();
}

QString dateToJson(const QDateTime& date) {
    return date.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime dateFromJson(const QString& value) {
    QDateTime date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(value, Qt::ISODate);
    }
    return date.isValid() ? date.toUTC() : QDateTime::currentDateTimeUtc();
}
} // namespace

Result<QByteArray> ProjectSerializer::serialize(const TierProject& project) const {
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    root.insert(QStringLiteral("app"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("TierListMaker")},
                            {QStringLiteral("version"), QStringLiteral("0.1.0")}});

    root.insert(QStringLiteral("project"),
                QJsonObject{{QStringLiteral("id"), project.id},
                            {QStringLiteral("name"), project.name},
                            {QStringLiteral("createdAt"), dateToJson(project.createdAt)},
                            {QStringLiteral("updatedAt"), dateToJson(project.updatedAt)},
                            {QStringLiteral("thumbnailPath"), project.thumbnailPath},
                            {QStringLiteral("canvas"), project.canvas}});

    QJsonArray tiers;
    for (const TierRow& row : project.rows) {
        QJsonArray imageIds;
        for (const QString& imageId : row.imageIds) {
            imageIds.append(imageId);
        }
        tiers.append(QJsonObject{{QStringLiteral("id"), row.id},
                                 {QStringLiteral("label"), row.label},
                                 {QStringLiteral("color"), row.color.name(QColor::HexRgb)},
                                 {QStringLiteral("order"), row.order},
                                 {QStringLiteral("height"), row.height},
                                 {QStringLiteral("imageIds"), imageIds}});
    }
    root.insert(QStringLiteral("tiers"), tiers);

    QJsonArray images;
    for (const TierImage& image : project.images) {
        QJsonObject object{{QStringLiteral("id"), image.id},
                           {QStringLiteral("sourcePath"), image.sourcePath},
                           {QStringLiteral("assetPath"), image.importedAssetPath},
                           {QStringLiteral("originalFileName"), image.originalFileName},
                           {QStringLiteral("displayName"), image.displayName},
                           {QStringLiteral("width"), image.width},
                           {QStringLiteral("height"), image.height},
                           {QStringLiteral("thumbnailPath"), image.thumbnailPath},
                           {QStringLiteral("order"), image.order}};
        if (image.hasCropRect()) {
            object.insert(QStringLiteral("crop"),
                          QJsonObject{{QStringLiteral("x"), image.cropRect.x()},
                                      {QStringLiteral("y"), image.cropRect.y()},
                                      {QStringLiteral("width"), image.cropRect.width()},
                                      {QStringLiteral("height"), image.cropRect.height()}});
        }
        if (image.assignedTierRowId.has_value()) {
            object.insert(QStringLiteral("assignedTierRowId"), *image.assignedTierRowId);
        } else {
            object.insert(QStringLiteral("assignedTierRowId"), QJsonValue::Null);
        }
        images.append(object);
    }
    root.insert(QStringLiteral("images"), images);
    root.insert(QStringLiteral("settings"), project.settings);

    return Result<QByteArray>::success(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

Result<TierProject> ProjectSerializer::deserialize(const QByteArray& data, const QString& filePath) const {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Result<TierProject>::failure(QObject::tr("The project file is not valid JSON."),
                                            parseError.errorString());
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != schemaVersion) {
        return Result<TierProject>::failure(QObject::tr("Unsupported project schema version."));
    }

    QString error;
    const QJsonObject projectObject = root.value(QStringLiteral("project")).toObject();
    TierProject project;
    project.id = requiredString(projectObject, QStringLiteral("id"), &error);
    if (!error.isEmpty()) {
        return Result<TierProject>::failure(QObject::tr("Malformed project metadata."), error);
    }
    project.name = requiredString(projectObject, QStringLiteral("name"), &error);
    if (!error.isEmpty()) {
        return Result<TierProject>::failure(QObject::tr("Malformed project metadata."), error);
    }
    project.createdAt = dateFromJson(projectObject.value(QStringLiteral("createdAt")).toString());
    project.updatedAt = dateFromJson(projectObject.value(QStringLiteral("updatedAt")).toString());
    project.thumbnailPath = projectObject.value(QStringLiteral("thumbnailPath")).toString();
    project.canvas = projectObject.value(QStringLiteral("canvas")).toObject();
    project.filePath = filePath;

    const QJsonArray tierArray = root.value(QStringLiteral("tiers")).toArray();
    if (tierArray.isEmpty()) {
        return Result<TierProject>::failure(QObject::tr("The project has no tier rows."));
    }
    for (const QJsonValue& value : tierArray) {
        const QJsonObject object = value.toObject();
        TierRow row;
        row.id = requiredString(object, QStringLiteral("id"), &error);
        row.label = requiredString(object, QStringLiteral("label"), &error);
        row.color = QColor(object.value(QStringLiteral("color")).toString(QStringLiteral("#999999")));
        row.order = requiredInt(object, QStringLiteral("order"), &error);
        row.height = object.value(QStringLiteral("height")).toInt(88);
        for (const QJsonValue& idValue : object.value(QStringLiteral("imageIds")).toArray()) {
            if (idValue.isString()) {
                row.imageIds.append(idValue.toString());
            }
        }
        if (!error.isEmpty() || row.id.isEmpty()) {
            return Result<TierProject>::failure(QObject::tr("Malformed tier row."), error);
        }
        project.rows.append(row);
    }

    const QJsonArray imageArray = root.value(QStringLiteral("images")).toArray();
    for (const QJsonValue& value : imageArray) {
        const QJsonObject object = value.toObject();
        TierImage image;
        image.id = requiredString(object, QStringLiteral("id"), &error);
        image.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
        image.importedAssetPath = object.value(QStringLiteral("assetPath")).toString();
        image.originalFileName = object.value(QStringLiteral("originalFileName")).toString();
        image.displayName = object.value(QStringLiteral("displayName")).toString(image.originalFileName);
        image.width = object.value(QStringLiteral("width")).toInt();
        image.height = object.value(QStringLiteral("height")).toInt();
        image.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
        image.order = object.value(QStringLiteral("order")).toInt();
        const QJsonObject crop = object.value(QStringLiteral("crop")).toObject();
        if (!crop.isEmpty()) {
            image.cropRect = QRectF(crop.value(QStringLiteral("x")).toDouble(),
                                    crop.value(QStringLiteral("y")).toDouble(),
                                    crop.value(QStringLiteral("width")).toDouble(),
                                    crop.value(QStringLiteral("height")).toDouble());
        }
        const QJsonValue assigned = object.value(QStringLiteral("assignedTierRowId"));
        if (assigned.isString() && !assigned.toString().isEmpty()) {
            image.assignedTierRowId = assigned.toString();
        }
        if (!error.isEmpty() || image.id.isEmpty()) {
            return Result<TierProject>::failure(QObject::tr("Malformed image metadata."), error);
        }
        project.images.append(image);
    }

    project.settings = root.value(QStringLiteral("settings")).toObject();
    project.normalizeOrdering();
    project.dirty = false;
    return Result<TierProject>::success(project);
}

} // namespace tlm
