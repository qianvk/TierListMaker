#include "persistence/RecentProjectsStore.h"

#include "persistence/AtomicFileWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace tlm {

namespace {
QString canonicalOrAbsolute(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QJsonObject toJson(const RecentProjectEntry& entry) {
    return QJsonObject{{QStringLiteral("filePath"), entry.filePath},
                       {QStringLiteral("name"), entry.name},
                       {QStringLiteral("thumbnailPath"), entry.thumbnailPath},
                       {QStringLiteral("backgroundImagePath"), entry.backgroundImagePath},
                       {QStringLiteral("createdAt"), entry.createdAt.toUTC().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("updatedAt"), entry.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("rowCount"), entry.rowCount},
                       {QStringLiteral("imageCount"), entry.imageCount}};
}

RecentProjectEntry fromJson(const QJsonObject& object) {
    RecentProjectEntry entry;
    entry.filePath = object.value(QStringLiteral("filePath")).toString();
    entry.name = object.value(QStringLiteral("name")).toString();
    entry.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    entry.backgroundImagePath = object.value(QStringLiteral("backgroundImagePath")).toString();
    entry.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(),
                                            Qt::ISODateWithMs);
    entry.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(),
                                            Qt::ISODateWithMs);
    entry.rowCount = object.value(QStringLiteral("rowCount")).toInt();
    entry.imageCount = object.value(QStringLiteral("imageCount")).toInt();
    return entry;
}
} // namespace

RecentProjectsStore::RecentProjectsStore(QString storePath, QObject* parent)
    : QObject(parent), m_storePath(storePath.isEmpty() ? defaultStorePath() : std::move(storePath)) {
    load();
}

QVector<RecentProjectEntry> RecentProjectsStore::entries() const {
    QVector<RecentProjectEntry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(), [](const RecentProjectEntry& lhs,
                                               const RecentProjectEntry& rhs) {
        return lhs.updatedAt > rhs.updatedAt;
    });
    return sorted;
}

Result<void> RecentProjectsStore::addOrUpdate(const TierProject& project) {
    if (project.filePath.isEmpty()) {
        return Result<void>::success();
    }
    RecentProjectEntry entry;
    entry.filePath = canonicalOrAbsolute(project.filePath);
    entry.name = project.name;
    entry.thumbnailPath = project.thumbnailPath;
    entry.backgroundImagePath = project.canvas.value(QStringLiteral("backgroundImagePath")).toString();
    entry.createdAt = project.createdAt;
    entry.updatedAt = project.updatedAt;
    entry.rowCount = static_cast<int>(project.rows.size());
    entry.imageCount = static_cast<int>(project.images.size());

    const int existing = indexOf(entry.filePath);
    if (existing >= 0) {
        m_entries[existing] = entry;
    } else {
        m_entries.prepend(entry);
    }
    while (m_entries.size() > 100) {
        m_entries.removeLast();
    }
    auto result = save();
    if (result) {
        emit changed();
    }
    return result;
}

Result<void> RecentProjectsStore::remove(const QString& filePath) {
    const int index = indexOf(canonicalOrAbsolute(filePath));
    if (index >= 0) {
        m_entries.removeAt(index);
        auto result = save();
        if (result) {
            emit changed();
        }
        return result;
    }
    return Result<void>::success();
}

Result<void> RecentProjectsStore::renameDisplayName(const QString& filePath, const QString& name) {
    const int index = indexOf(canonicalOrAbsolute(filePath));
    if (index >= 0) {
        m_entries[index].name = name;
        m_entries[index].updatedAt = QDateTime::currentDateTimeUtc();
        auto result = save();
        if (result) {
            emit changed();
        }
        return result;
    }
    return Result<void>::failure(tr("The project is not in the recent list."));
}

Result<void> RecentProjectsStore::load() {
    m_entries.clear();
    QFile file(m_storePath);
    if (!file.exists()) {
        return Result<void>::success();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<void>::failure(tr("Could not read recent projects."), file.errorString());
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return Result<void>::failure(tr("Recent projects metadata is malformed."));
    }
    for (const QJsonValue& value : document.array()) {
        const RecentProjectEntry entry = fromJson(value.toObject());
        if (!entry.filePath.isEmpty()) {
            m_entries.append(entry);
        }
    }
    return Result<void>::success();
}

Result<void> RecentProjectsStore::save() const {
    QJsonArray array;
    for (const RecentProjectEntry& entry : m_entries) {
        array.append(toJson(entry));
    }
    return AtomicFileWriter::write(m_storePath, QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QString RecentProjectsStore::defaultStorePath() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("recent-projects.json"));
}

int RecentProjectsStore::indexOf(const QString& filePath) const {
    const QString normalized = canonicalOrAbsolute(filePath);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (canonicalOrAbsolute(m_entries[i].filePath) == normalized) {
            return i;
        }
    }
    return -1;
}

} // namespace tlm
