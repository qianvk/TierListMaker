#include "update/AppUpdater.h"

#include "logging/Logger.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

#ifndef TLM_APP_VERSION
#define TLM_APP_VERSION "0.1.0"
#endif

namespace tlm {

namespace {
constexpr auto kDefaultProjectUrl = "https://github.com/qianvk/TierListMaker";
constexpr auto kDefaultDefinitionUrl =
    "https://github.com/qianvk/TierListMaker/releases/latest/download/updates.json";

QString platformKey() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("osx");
#else
    return QStringLiteral("linux");
#endif
}

QString normalizedVersion(QString version) {
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

QString stringValue(const QJsonObject& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isString()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

bool boolValue(const QJsonObject& object, std::initializer_list<const char*> keys, bool fallback = false) {
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed().toLower();
            if (text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes")) {
                return true;
            }
            if (text == QStringLiteral("false") || text == QStringLiteral("0") || text == QStringLiteral("no")) {
                return false;
            }
        }
    }
    return fallback;
}

QUrl urlValue(const QJsonObject& object, std::initializer_list<const char*> keys) {
    const QString value = stringValue(object, keys);
    return value.isEmpty() ? QUrl() : QUrl(value);
}

QJsonObject platformObject(const QJsonObject& root) {
    const QString key = platformKey();
    const QStringList candidates = key == QStringLiteral("osx")
                                       ? QStringList{QStringLiteral("osx"), QStringLiteral("macos"),
                                                     QStringLiteral("mac"), QStringLiteral("darwin"),
                                                     QStringLiteral("all"), QStringLiteral("default")}
                                       : QStringList{key, QStringLiteral("all"), QStringLiteral("default")};

    for (const QString& candidate : candidates) {
        const QJsonValue value = root.value(candidate);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return {};
}

int preReleaseRank(const QString& tag) {
    if (tag.isEmpty()) {
        return 4;
    }
    const QString lower = tag.toLower();
    if (lower.startsWith(QStringLiteral("alpha"))) {
        return 1;
    }
    if (lower.startsWith(QStringLiteral("beta"))) {
        return 2;
    }
    if (lower.startsWith(QStringLiteral("rc"))) {
        return 3;
    }
    return 0;
}

int compareVersions(const QString& leftVersion, const QString& rightVersion) {
    const QStringList leftParts = normalizedVersion(leftVersion).split(QLatin1Char('-'));
    const QStringList rightParts = normalizedVersion(rightVersion).split(QLatin1Char('-'));
    const QStringList leftNumbers = leftParts.value(0).split(QLatin1Char('.'));
    const QStringList rightNumbers = rightParts.value(0).split(QLatin1Char('.'));
    const qsizetype count = qMax(leftNumbers.size(), rightNumbers.size());

    for (qsizetype i = 0; i < count; ++i) {
        bool leftOk = false;
        bool rightOk = false;
        const int left = leftNumbers.value(i).toInt(&leftOk);
        const int right = rightNumbers.value(i).toInt(&rightOk);
        if (leftOk && rightOk && left != right) {
            return left < right ? -1 : 1;
        }
        if (leftNumbers.value(i) != rightNumbers.value(i)) {
            return QString::localeAwareCompare(leftNumbers.value(i), rightNumbers.value(i));
        }
    }

    const QString leftPre = leftParts.size() > 1 ? leftParts.mid(1).join(QLatin1Char('-')) : QString();
    const QString rightPre = rightParts.size() > 1 ? rightParts.mid(1).join(QLatin1Char('-')) : QString();
    const int leftRank = preReleaseRank(leftPre);
    const int rightRank = preReleaseRank(rightPre);
    if (leftRank != rightRank) {
        return leftRank < rightRank ? -1 : 1;
    }
    return QString::localeAwareCompare(leftPre, rightPre);
}

UpdateCheckResult parseQSimpleUpdaterDefinition(const QJsonObject& root, const QString& currentVersion) {
    QJsonObject object = root;
    if (root.value(QStringLiteral("updates")).isObject()) {
        object = platformObject(root.value(QStringLiteral("updates")).toObject());
    } else {
        const QJsonObject platform = platformObject(root);
        if (!platform.isEmpty()) {
            object = platform;
        }
    }

    UpdateCheckResult result;
    result.currentVersion = currentVersion;
    result.latestVersion = normalizedVersion(
        stringValue(object, {"latest-version", "latestVersion", "version", "tag_name"}));
    result.downloadUrl = urlValue(object, {"download-url", "downloadUrl", "url"});
    result.openUrl = urlValue(object, {"open-url", "openUrl", "release-url", "releaseUrl"});
    result.changelog = stringValue(object, {"changelog", "changes", "notes", "body"});
    result.mandatory = boolValue(object, {"mandatory-update", "mandatoryUpdate", "mandatory"}, false);
    result.updateAvailable =
        !result.latestVersion.isEmpty() && compareVersions(currentVersion, result.latestVersion) < 0;
    if (!result.openUrl.isValid()) {
        result.openUrl = result.downloadUrl.isValid() ? result.downloadUrl : AppUpdater::defaultProjectUrl();
    }
    return result;
}

UpdateCheckResult parseGitHubRelease(const QJsonObject& root, const QString& currentVersion) {
    UpdateCheckResult result;
    result.currentVersion = currentVersion;
    result.latestVersion = normalizedVersion(stringValue(root, {"tag_name", "name"}));
    result.openUrl = urlValue(root, {"html_url"});
    result.changelog = stringValue(root, {"body"});

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    if (!assets.isEmpty()) {
        const QString key = platformKey();
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString().toLower();
            if (name.contains(key) || (key == QStringLiteral("osx") && name.contains(QStringLiteral("mac")))) {
                result.downloadUrl = urlValue(asset, {"browser_download_url"});
                break;
            }
        }
        if (!result.downloadUrl.isValid()) {
            result.downloadUrl = urlValue(assets.first().toObject(), {"browser_download_url"});
        }
    }

    result.updateAvailable =
        !result.latestVersion.isEmpty() && compareVersions(currentVersion, result.latestVersion) < 0;
    if (!result.openUrl.isValid()) {
        result.openUrl = AppUpdater::defaultProjectUrl();
    }
    return result;
}

UpdateCheckResult parseUpdatePayload(const QByteArray& payload, const QString& currentVersion,
                                     QString* error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QObject::tr("The update response is not valid JSON.");
        }
        return {};
    }

    const QJsonObject root = document.object();
    UpdateCheckResult result =
        root.contains(QStringLiteral("tag_name")) ? parseGitHubRelease(root, currentVersion)
                                                  : parseQSimpleUpdaterDefinition(root, currentVersion);
    if (result.latestVersion.isEmpty()) {
        if (error) {
            *error = QObject::tr("The update response does not contain a latest version.");
        }
        return {};
    }
    return result;
}
} // namespace

AppUpdater::AppUpdater(QObject* parent) : QObject(parent) {
    qRegisterMetaType<tlm::UpdateCheckResult>("tlm::UpdateCheckResult");
}

QUrl AppUpdater::defaultUpdateDefinitionUrl() {
    return QUrl(QString::fromLatin1(kDefaultDefinitionUrl));
}

QUrl AppUpdater::defaultProjectUrl() {
    return QUrl(QString::fromLatin1(kDefaultProjectUrl));
}

bool AppUpdater::isChecking() const {
    return m_reply && m_reply->isRunning();
}

void AppUpdater::checkForUpdates(const QUrl& definitionUrl) {
    if (isChecking()) {
        Logger::debug(QStringLiteral("app.update.check.skip reason=already-running"));
        return;
    }

    const QUrl url = definitionUrl.isValid() ? definitionUrl : defaultUpdateDefinitionUrl();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TierListMaker/%1 (QSimpleUpdater-compatible)")
                          .arg(QStringLiteral(TLM_APP_VERSION)));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    Logger::info(QStringLiteral("app.update.check.start url=\"%1\"").arg(url.toString()));
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &AppUpdater::finishReply);
    emit checkingStarted(url);
}

void AppUpdater::openUpdatePage(const UpdateCheckResult& result) {
    QUrl url = result.downloadUrl.isValid() ? result.downloadUrl : result.openUrl;
    if (!url.isValid()) {
        url = defaultProjectUrl();
    }
    Logger::info(QStringLiteral("app.update.open url=\"%1\"").arg(url.toString()));
    QDesktopServices::openUrl(url);
}

void AppUpdater::finishReply() {
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        return;
    }

    const QUrl url = reply->url();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()
            ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            : 0;
    const QByteArray payload = reply->readAll();
    const QString networkError = reply->errorString();
    const bool failed = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();

    if (failed) {
        Logger::warn(QStringLiteral("app.update.check.failed url=\"%1\" status=%2 error=\"%3\"")
                         .arg(url.toString())
                         .arg(status)
                         .arg(networkError));
        emit checkFailed(networkError);
        return;
    }

    QString parseError;
    UpdateCheckResult result =
        parseUpdatePayload(payload, QStringLiteral(TLM_APP_VERSION), &parseError);
    if (!parseError.isEmpty()) {
        Logger::warn(QStringLiteral("app.update.check.invalid url=\"%1\" status=%2 error=\"%3\"")
                         .arg(url.toString())
                         .arg(status)
                         .arg(parseError));
        emit checkFailed(parseError);
        return;
    }

    Logger::info(QStringLiteral("app.update.check.finish current=%1 latest=%2 available=%3 mandatory=%4")
                     .arg(result.currentVersion, result.latestVersion)
                     .arg(result.updateAvailable)
                     .arg(result.mandatory));
    m_lastResult = result;
    if (m_hasUpdateAvailable != result.updateAvailable) {
        m_hasUpdateAvailable = result.updateAvailable;
        emit updateNotificationChanged(m_hasUpdateAvailable);
    }
    if (result.updateAvailable) {
        emit updateAvailable(result);
    } else {
        emit noUpdateAvailable(result);
    }
}

} // namespace tlm
