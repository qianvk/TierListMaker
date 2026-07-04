#include "tier/TierProject.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace tlm {

namespace {
QVector<TierRow> makeDefaultRows() {
    return {
        TierRow::makeDefault(QStringLiteral("S"), QColor(QStringLiteral("#ff7b7b")), 0),
        TierRow::makeDefault(QStringLiteral("A"), QColor(QStringLiteral("#ffc36b")), 1),
        TierRow::makeDefault(QStringLiteral("B"), QColor(QStringLiteral("#ffe17d")), 2),
        TierRow::makeDefault(QStringLiteral("C"), QColor(QStringLiteral("#8bdc8b")), 3),
        TierRow::makeDefault(QStringLiteral("D"), QColor(QStringLiteral("#82b7ff")), 4),
    };
}
} // namespace

TierProject TierProject::createUntitled() {
    TierProject project;
    project.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    project.name = QObject::tr("Untitled Tier List");
    project.createdAt = QDateTime::currentDateTimeUtc();
    project.updatedAt = project.createdAt;
    project.rows = makeDefaultRows();
    project.settings.insert(QStringLiteral("background"), QStringLiteral("default"));
    project.settings.insert(QStringLiteral("exportScale"), 2);
    project.dirty = false;
    return project;
}

TierRow* TierProject::rowById(const QString& rowId) {
    auto it = std::find_if(rows.begin(), rows.end(), [&](const TierRow& row) { return row.id == rowId; });
    return it == rows.end() ? nullptr : &(*it);
}

const TierRow* TierProject::rowById(const QString& rowId) const {
    auto it = std::find_if(rows.cbegin(), rows.cend(), [&](const TierRow& row) { return row.id == rowId; });
    return it == rows.cend() ? nullptr : &(*it);
}

TierImage* TierProject::imageById(const QString& imageId) {
    auto it = std::find_if(images.begin(), images.end(),
                           [&](const TierImage& image) { return image.id == imageId; });
    return it == images.end() ? nullptr : &(*it);
}

const TierImage* TierProject::imageById(const QString& imageId) const {
    auto it = std::find_if(images.cbegin(), images.cend(),
                           [&](const TierImage& image) { return image.id == imageId; });
    return it == images.cend() ? nullptr : &(*it);
}

QVector<TierImage*> TierProject::unassignedImages() {
    QVector<TierImage*> result;
    for (TierImage& image : images) {
        if (!image.assignedTierRowId.has_value()) {
            result.push_back(&image);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const TierImage* lhs, const TierImage* rhs) { return lhs->order < rhs->order; });
    return result;
}

QVector<const TierImage*> TierProject::unassignedImages() const {
    QVector<const TierImage*> result;
    for (const TierImage& image : images) {
        if (!image.assignedTierRowId.has_value()) {
            result.push_back(&image);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const TierImage* lhs, const TierImage* rhs) { return lhs->order < rhs->order; });
    return result;
}

QVector<const TierImage*> TierProject::imagesForRow(const QString& rowId) const {
    QVector<const TierImage*> result;
    const TierRow* row = rowById(rowId);
    if (!row) {
        return result;
    }
    for (const QString& imageId : row->imageIds) {
        if (const TierImage* image = imageById(imageId)) {
            result.push_back(image);
        }
    }
    return result;
}

void TierProject::resetDefaultRows() {
    rows = makeDefaultRows();
    for (TierImage& image : images) {
        image.assignedTierRowId.reset();
    }
    normalizeOrdering();
    touch();
}

void TierProject::normalizeOrdering() {
    std::sort(rows.begin(), rows.end(), [](const TierRow& lhs, const TierRow& rhs) {
        return lhs.order < rhs.order;
    });
    for (int i = 0; i < rows.size(); ++i) {
        rows[i].order = i;
        QStringList valid;
        for (const QString& imageId : rows[i].imageIds) {
            if (TierImage* image = imageById(imageId)) {
                image->assignedTierRowId = rows[i].id;
                image->order = static_cast<int>(valid.size());
                valid.push_back(imageId);
            }
        }
        rows[i].imageIds = valid;
    }
    int unassignedOrder = 0;
    for (TierImage& image : images) {
        if (!image.assignedTierRowId.has_value() || !rowById(*image.assignedTierRowId)) {
            image.assignedTierRowId.reset();
            image.order = unassignedOrder++;
        }
    }
}

void TierProject::touch() {
    updatedAt = QDateTime::currentDateTimeUtc();
    dirty = true;
}

QString TierProject::suggestedFileName() const {
    QString base = name.trimmed();
    if (base.isEmpty()) {
        base = QStringLiteral("Untitled Tier List");
    }
    base.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|]+)")), QStringLiteral("-"));
    if (!base.endsWith(QStringLiteral(".tlmproject"), Qt::CaseInsensitive)) {
        base += QStringLiteral(".tlmproject");
    }
    return base;
}

} // namespace tlm
