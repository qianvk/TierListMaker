#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

namespace tlm {

struct UpdateCheckResult {
    bool updateAvailable{false};
    bool mandatory{false};
    QString currentVersion;
    QString latestVersion;
    QString changelog;
    QUrl downloadUrl;
    QUrl openUrl;
};

/** QSimpleUpdater-compatible update checker with a GitHub Releases fallback parser. */
class AppUpdater : public QObject {
    Q_OBJECT

public:
    explicit AppUpdater(QObject* parent = nullptr);

    static QUrl defaultUpdateDefinitionUrl();
    static QUrl defaultProjectUrl();

    bool isChecking() const;
    bool hasUpdateAvailable() const { return m_hasUpdateAvailable; }
    UpdateCheckResult lastResult() const { return m_lastResult; }

public slots:
    void checkForUpdates(const QUrl& definitionUrl = {});
    void openUpdatePage(const tlm::UpdateCheckResult& result = {});

signals:
    void checkingStarted(const QUrl& definitionUrl);
    void updateAvailable(const tlm::UpdateCheckResult& result);
    void noUpdateAvailable(const tlm::UpdateCheckResult& result);
    void checkFailed(const QString& reason);
    void updateNotificationChanged(bool visible);

private:
    void finishReply();

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    UpdateCheckResult m_lastResult;
    bool m_hasUpdateAvailable{false};
};

} // namespace tlm

Q_DECLARE_METATYPE(tlm::UpdateCheckResult)
