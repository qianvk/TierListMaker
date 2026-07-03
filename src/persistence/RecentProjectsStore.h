#pragma once

#include "persistence/Result.h"
#include "tier/TierProject.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace tlm {

/** Lightweight metadata shown on the Projects page. */
struct RecentProjectEntry {
    QString filePath;
    QString name;
    QString thumbnailPath;
    QString backgroundImagePath;
    QDateTime createdAt;
    QDateTime updatedAt;
    int rowCount{0};
    int imageCount{0};
};

/** Stores recent project metadata locally without scanning user disks. */
class RecentProjectsStore : public QObject {
    Q_OBJECT

public:
    explicit RecentProjectsStore(QString storePath = {}, QObject* parent = nullptr);

    QVector<RecentProjectEntry> entries() const;
    Result<void> addOrUpdate(const TierProject& project);
    Result<void> remove(const QString& filePath);
    Result<void> renameDisplayName(const QString& filePath, const QString& name);
    Result<void> load();
    Result<void> save() const;
    QString storePath() const { return m_storePath; }

signals:
    void changed();

private:
    static QString defaultStorePath();
    int indexOf(const QString& filePath) const;

    QString m_storePath;
    QVector<RecentProjectEntry> m_entries;
};

} // namespace tlm
