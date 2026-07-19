#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

class QCryptographicHash;
class QNetworkReply;
class QSaveFile;

namespace tlm {

enum class UpdateState {
    Idle,
    Checking,
    Available,
    Downloading,
    Ready,
    Installing,
    Error,
};

struct UpdateCheckResult {
    bool updateAvailable{false};
    bool mandatory{false};
    QString currentVersion;
    QString latestVersion;
    QString channel;
    QString runtimeVersion;
    QString changelog;
    QMap<QString, QString> localizedChangelogs;
    QString fileName;
    QString sha256;
    qint64 packageSize{-1};
    QUrl downloadUrl;
    QUrl openUrl;
    QUrl metadataUrl;
    QUrl updateDownloadUrl;
    QString updateFileName;
    QString updateSha256;
    qint64 updatePackageSize{-1};
    bool lightweightPackage{false};
};

/** Checks release metadata and stages checksum-verified platform installers. */
class AppUpdater : public QObject {
    Q_OBJECT

public:
    explicit AppUpdater(QObject* parent = nullptr);
    ~AppUpdater() override;

    static QUrl defaultUpdateDefinitionUrl();
    static QUrl defaultProjectUrl();
    static QString updateChannel();
    static QString runtimeVersion();
    static int compareVersions(const QString& left, const QString& right);
    static UpdateCheckResult parseUpdatePayload(const QByteArray& payload,
                                                const QString& currentVersion, QString* error,
                                                const QString& language = {});

    bool isChecking() const;
    bool isDownloading() const;
    bool hasUpdateAvailable() const {
        return m_hasUpdateAvailable;
    }
    UpdateState state() const {
        return m_state;
    }
    UpdateCheckResult lastResult() const {
        return m_lastResult;
    }
    QString downloadedPackagePath() const {
        return m_downloadedPackagePath;
    }
    QString language() const {
        return m_language;
    }

public slots:
    void setLanguage(const QString& language);
    void checkForUpdates(const QUrl& definitionUrl = {});
    void cancelCheck();
    void startUpdate();
    void openUpdatePage(const tlm::UpdateCheckResult& result = {});

signals:
    void checkingStarted(const QUrl& definitionUrl);
    void updateAvailable(const tlm::UpdateCheckResult& result);
    void noUpdateAvailable(const tlm::UpdateCheckResult& result);
    void updateDetailsChanged(const tlm::UpdateCheckResult& result);
    void checkFailed(const QString& reason);
    void updateNotificationChanged(bool visible);
    void stateChanged(tlm::UpdateState state);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updateReady(const QString& packagePath);
    void updateFailed(const QString& reason);

private:
    void appendCheckData();
    void finishCheckReply();
    void beginMetadataCheck(const UpdateCheckResult& releaseResult);
    void finishMetadataReply();
    void completeCheck(UpdateCheckResult result);
    void beginDownload();
    void readDownloadData();
    void finishDownloadReply();
    void installDownloadedUpdate();
    void failDownload(const QString& reason);
    void setState(UpdateState state);
    bool useCachedPackage(const UpdateCheckResult& result);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    std::unique_ptr<QSaveFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    QByteArray m_checkPayload;
    std::optional<UpdateCheckResult> m_pendingReleaseResult;
    UpdateCheckResult m_lastResult;
    QString m_language;
    QString m_downloadedPackagePath;
    qint64 m_downloadedBytes{0};
    UpdateState m_state{UpdateState::Idle};
    bool m_hasUpdateAvailable{false};
};

} // namespace tlm

Q_DECLARE_METATYPE(tlm::UpdateCheckResult)
Q_DECLARE_METATYPE(tlm::UpdateState)
