#include "update/AppUpdater.h"

#include "logging/Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

#ifndef TLM_APP_VERSION
#define TLM_APP_VERSION "0.2.0-beta.4"
#endif

#ifndef TLM_UPDATE_CHANNEL
#define TLM_UPDATE_CHANNEL "beta"
#endif

#ifndef TLM_UPDATE_MANIFEST_URL
#define TLM_UPDATE_MANIFEST_URL                                                                  \
    "https://api.github.com/repos/qianvk/TierListMaker/releases?per_page=20"
#endif

namespace tlm {

namespace {
constexpr auto kDefaultProjectUrl = "https://github.com/qianvk/TierListMaker";
constexpr auto kDefaultDefinitionUrl = TLM_UPDATE_MANIFEST_URL;
constexpr qsizetype kMaximumManifestBytes = 1024 * 1024;
constexpr qint64 kMaximumPackageBytes = 1024LL * 1024LL * 1024LL;
constexpr int kManifestTimeoutMs = 15'000;
constexpr int kPackageTimeoutMs = 30'000;

struct SemanticVersion {
    QVector<quint64> core;
    QStringList prerelease;
    bool valid{false};
};

QString platformKey() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("macos");
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

SemanticVersion parseSemanticVersion(QString version) {
    SemanticVersion parsed;
    version = normalizedVersion(std::move(version));
    const qsizetype buildSeparator = version.indexOf(QLatin1Char('+'));
    if (buildSeparator >= 0) {
        version.truncate(buildSeparator);
    }

    QString coreText = version;
    const qsizetype prereleaseSeparator = version.indexOf(QLatin1Char('-'));
    if (prereleaseSeparator >= 0) {
        coreText = version.first(prereleaseSeparator);
        const QString prereleaseText = version.sliced(prereleaseSeparator + 1);
        parsed.prerelease = prereleaseText.split(QLatin1Char('.'), Qt::KeepEmptyParts);
        static const QRegularExpression identifierPattern(QStringLiteral("^[0-9A-Za-z-]+$"));
        for (const QString& identifier : std::as_const(parsed.prerelease)) {
            if (identifier.isEmpty() || !identifierPattern.match(identifier).hasMatch()) {
                return parsed;
            }
        }
    }

    const QStringList coreParts = coreText.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    if (coreParts.isEmpty()) {
        return parsed;
    }
    parsed.core.reserve(coreParts.size());
    for (const QString& part : coreParts) {
        if (part.isEmpty() || (part.size() > 1 && part.startsWith(QLatin1Char('0')))) {
            return parsed;
        }
        bool ok = false;
        const quint64 number = part.toULongLong(&ok);
        if (!ok) {
            return parsed;
        }
        parsed.core.append(number);
    }
    parsed.valid = true;
    return parsed;
}

int compareSemanticVersions(const QString& leftText, const QString& rightText) {
    const SemanticVersion left = parseSemanticVersion(leftText);
    const SemanticVersion right = parseSemanticVersion(rightText);
    if (!left.valid || !right.valid) {
        return QString::compare(normalizedVersion(leftText), normalizedVersion(rightText),
                                Qt::CaseSensitive);
    }

    const qsizetype coreCount = qMax(left.core.size(), right.core.size());
    for (qsizetype i = 0; i < coreCount; ++i) {
        const quint64 leftPart = i < left.core.size() ? left.core.at(i) : 0;
        const quint64 rightPart = i < right.core.size() ? right.core.at(i) : 0;
        if (leftPart != rightPart) {
            return leftPart < rightPart ? -1 : 1;
        }
    }

    if (left.prerelease.isEmpty() != right.prerelease.isEmpty()) {
        return left.prerelease.isEmpty() ? 1 : -1;
    }
    const qsizetype identifierCount = qMin(left.prerelease.size(), right.prerelease.size());
    for (qsizetype i = 0; i < identifierCount; ++i) {
        const QString& leftIdentifier = left.prerelease.at(i);
        const QString& rightIdentifier = right.prerelease.at(i);
        if (leftIdentifier == rightIdentifier) {
            continue;
        }
        bool leftNumeric = false;
        bool rightNumeric = false;
        const quint64 leftNumber = leftIdentifier.toULongLong(&leftNumeric);
        const quint64 rightNumber = rightIdentifier.toULongLong(&rightNumeric);
        if (leftNumeric && rightNumeric) {
            return leftNumber < rightNumber ? -1 : 1;
        }
        if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        }
        return QString::compare(leftIdentifier, rightIdentifier, Qt::CaseSensitive) < 0 ? -1 : 1;
    }
    if (left.prerelease.size() == right.prerelease.size()) {
        return 0;
    }
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
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

bool boolValue(const QJsonObject& object, std::initializer_list<const char*> keys,
               bool fallback = false) {
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed().toLower();
            if (text == QStringLiteral("true") || text == QStringLiteral("1") ||
                text == QStringLiteral("yes")) {
                return true;
            }
            if (text == QStringLiteral("false") || text == QStringLiteral("0") ||
                text == QStringLiteral("no")) {
                return false;
            }
        }
    }
    return fallback;
}

qint64 integerValue(const QJsonObject& object, std::initializer_list<const char*> keys,
                    qint64 fallback = -1) {
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isDouble()) {
            const double number = value.toDouble(-1.0);
            if (number >= 0.0 && number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
                return static_cast<qint64>(number);
            }
        } else if (value.isString()) {
            bool ok = false;
            const qint64 number = value.toString().toLongLong(&ok);
            if (ok && number >= 0) {
                return number;
            }
        }
    }
    return fallback;
}

QUrl urlValue(const QJsonObject& object, std::initializer_list<const char*> keys) {
    const QString value = stringValue(object, keys);
    return value.isEmpty() ? QUrl() : QUrl(value);
}

bool isSecureWebUrl(const QUrl& url) {
    return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           !url.host().isEmpty();
}

QJsonObject platformObject(const QJsonObject& root) {
    const QString key = platformKey();
    QStringList candidates{key};
    if (key == QStringLiteral("macos")) {
        candidates.append({QStringLiteral("osx"), QStringLiteral("mac"),
                           QStringLiteral("darwin")});
    }
    candidates.append({QStringLiteral("all"), QStringLiteral("default")});
    for (const QString& candidate : std::as_const(candidates)) {
        const QJsonValue value = root.value(candidate);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return {};
}

QString packageFileName(const UpdateCheckResult& result) {
    QString fileName = QFileInfo(result.fileName).fileName();
    if (fileName.isEmpty()) {
        fileName = QFileInfo(result.downloadUrl.path()).fileName();
    }
    static const QRegularExpression unsafeCharacters(QStringLiteral("[<>:\"/\\\\|?*]"));
    fileName.replace(unsafeCharacters, QStringLiteral("_"));
    return fileName;
}

bool packageTypeSupported(const QString& fileName) {
#if defined(Q_OS_WIN)
    return fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return fileName.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive) ||
           fileName.endsWith(QStringLiteral(".pkg"), Qt::CaseInsensitive);
#else
    return fileName.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive) ||
           fileName.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive);
#endif
}

bool packageMatchesPlatform(const QString& fileName) {
    const QString name = fileName.toLower();
#if defined(Q_OS_WIN)
    return name.contains(QStringLiteral("windows")) || name.contains(QStringLiteral("win64"));
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return name.contains(QStringLiteral("macos")) || name.contains(QStringLiteral("darwin")) ||
           name.contains(QStringLiteral("osx"));
#else
    return name.contains(QStringLiteral("linux"));
#endif
}

QStringList architectureAliases() {
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")) {
        return {QStringLiteral("x86_64"), QStringLiteral("x64"), QStringLiteral("amd64")};
    }
    if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")) {
        return {QStringLiteral("arm64"), QStringLiteral("aarch64")};
    }
    return {architecture};
}

bool packageMatchesArchitecture(const QString& fileName) {
    const QString name = fileName.toLower();
    if (name.contains(QStringLiteral("universal"))) {
        return true;
    }

    static const QStringList knownArchitectures{
        QStringLiteral("x86_64"), QStringLiteral("amd64"), QStringLiteral("x64"),
        QStringLiteral("aarch64"), QStringLiteral("arm64")};
    const bool declaresArchitecture = std::ranges::any_of(
        knownArchitectures, [&name](const QString& architecture) {
            return name.contains(architecture);
        });
    if (!declaresArchitecture) {
        return true;
    }
    return std::ranges::any_of(architectureAliases(), [&name](const QString& architecture) {
        return name.contains(architecture);
    });
}

QString packageCachePath(const UpdateCheckResult& result) {
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return QDir(cacheRoot).filePath(
        QStringLiteral("updates/%1/%2").arg(result.latestVersion, packageFileName(result)));
}

bool verifyPackage(const QString& path, const UpdateCheckResult& result) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (result.packageSize >= 0 && file.size() != result.packageSize) {
        return false;
    }
    if (result.sha256.size() != 64) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return false;
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex()) == result.sha256.toLower();
}

UpdateCheckResult parseDefinition(const QJsonObject& root, const QString& currentVersion,
                                  QString* error) {
    QJsonObject object = root;
    if (root.value(QStringLiteral("updates")).isObject()) {
        object = platformObject(root.value(QStringLiteral("updates")).toObject());
        if (object.isEmpty()) {
            if (error) {
                *error = QObject::tr("No update package is published for this platform.");
            }
            return {};
        }
    } else {
        const QJsonObject platform = platformObject(root);
        if (!platform.isEmpty()) {
            object = platform;
        }
    }

    UpdateCheckResult result;
    result.currentVersion = currentVersion;
    result.channel = stringValue(root, {"channel"});
    if (result.channel.isEmpty()) {
        result.channel = stringValue(object, {"channel"});
    }
    result.latestVersion = normalizedVersion(
        stringValue(object, {"latest-version", "latestVersion", "version", "tag_name"}));
    result.downloadUrl = urlValue(object, {"download-url", "downloadUrl", "url"});
    result.openUrl =
        urlValue(object, {"open-url", "openUrl", "release-url", "releaseUrl"});
    result.changelog = stringValue(object, {"changelog", "changes", "notes", "body"});
    result.fileName = stringValue(object, {"file-name", "fileName", "name"});
    result.sha256 = stringValue(object, {"sha256", "checksum"}).toLower();
    result.packageSize = integerValue(object, {"size", "package-size", "packageSize"});
    result.mandatory =
        boolValue(object, {"mandatory-update", "mandatoryUpdate", "mandatory"}, false);

    const SemanticVersion latestVersion = parseSemanticVersion(result.latestVersion);
    if (!latestVersion.valid) {
        if (error) {
            *error = QObject::tr("The update response does not contain a valid latest version.");
        }
        return {};
    }
    if (result.downloadUrl.isValid() && !isSecureWebUrl(result.downloadUrl)) {
        if (error) {
            *error = QObject::tr("The update package URL is not secure.");
        }
        return {};
    }
    if (result.openUrl.isValid() && !isSecureWebUrl(result.openUrl)) {
        if (error) {
            *error = QObject::tr("The update page URL is not secure.");
        }
        return {};
    }
    if (!result.sha256.isEmpty()) {
        static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
        if (!shaPattern.match(result.sha256).hasMatch()) {
            if (error) {
                *error = QObject::tr("The update package checksum is invalid.");
            }
            return {};
        }
    }

    const QString minimumVersion = normalizedVersion(stringValue(
        object, {"minimum-supported-version", "minimumSupportedVersion", "minimum-version"}));
    if (!minimumVersion.isEmpty() && parseSemanticVersion(minimumVersion).valid &&
        compareSemanticVersions(currentVersion, minimumVersion) < 0) {
        result.mandatory = true;
    }
    result.updateAvailable = compareSemanticVersions(currentVersion, result.latestVersion) < 0;
    if (result.updateAvailable && result.downloadUrl.isValid()) {
        const QString fileName = packageFileName(result);
        if (fileName.isEmpty() || !packageTypeSupported(fileName)) {
            if (error) {
                *error = QObject::tr("The update package type is not supported on this platform.");
            }
            return {};
        }
        result.fileName = fileName;
    }
    if (!result.openUrl.isValid()) {
        result.openUrl = AppUpdater::defaultProjectUrl();
    }
    return result;
}

UpdateCheckResult parseGitHubRelease(const QJsonObject& root, const QString& currentVersion,
                                     QString* error) {
    QJsonObject definition;
    definition.insert(QStringLiteral("version"),
                      stringValue(root, {"tag_name", "name"}));
    definition.insert(QStringLiteral("release-url"), stringValue(root, {"html_url"}));
    definition.insert(QStringLiteral("changelog"), stringValue(root, {"body"}));
    definition.insert(QStringLiteral("channel"),
                      root.value(QStringLiteral("prerelease")).toBool()
                          ? QStringLiteral("beta")
                          : QStringLiteral("stable"));

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString().trimmed();
        if (packageTypeSupported(name) && packageMatchesPlatform(name) &&
            packageMatchesArchitecture(name)) {
            definition.insert(QStringLiteral("download-url"),
                              stringValue(asset, {"browser_download_url"}));
            definition.insert(QStringLiteral("file-name"), name);
            definition.insert(QStringLiteral("size"), asset.value(QStringLiteral("size")));
            QString digest = stringValue(asset, {"digest"}).toLower();
            if (digest.startsWith(QStringLiteral("sha256:"))) {
                digest.remove(0, 7);
            }
            definition.insert(QStringLiteral("sha256"), digest);
            break;
        }
    }
    return parseDefinition(definition, currentVersion, error);
}

UpdateCheckResult parseGitHubReleaseList(const QJsonArray& releases,
                                         const QString& currentVersion, QString* error) {
    UpdateCheckResult bestResult;
    QString lastError;
    const bool includePrereleases =
        AppUpdater::updateChannel().compare(QStringLiteral("stable"), Qt::CaseInsensitive) != 0;

    for (const QJsonValue& value : releases) {
        const QJsonObject release = value.toObject();
        if (release.isEmpty() || release.value(QStringLiteral("draft")).toBool() ||
            (!includePrereleases && release.value(QStringLiteral("prerelease")).toBool())) {
            continue;
        }

        QString releaseError;
        const UpdateCheckResult candidate =
            parseGitHubRelease(release, currentVersion, &releaseError);
        if (!releaseError.isEmpty()) {
            lastError = releaseError;
            continue;
        }
        // Releases without a package for this platform are not actionable update candidates.
        if (!candidate.downloadUrl.isValid() || candidate.sha256.size() != 64 ||
            candidate.packageSize <= 0) {
            continue;
        }
        if (bestResult.latestVersion.isEmpty() ||
            compareSemanticVersions(candidate.latestVersion, bestResult.latestVersion) > 0) {
            bestResult = candidate;
        }
    }

    if (bestResult.latestVersion.isEmpty()) {
        if (error) {
            *error = lastError.isEmpty()
                         ? QObject::tr("No update package is published for this platform.")
                         : lastError;
        }
        return {};
    }
    return bestResult;
}
} // namespace

AppUpdater::AppUpdater(QObject* parent) : QObject(parent) {
    qRegisterMetaType<tlm::UpdateCheckResult>("tlm::UpdateCheckResult");
    qRegisterMetaType<tlm::UpdateState>("tlm::UpdateState");
}

AppUpdater::~AppUpdater() = default;

QUrl AppUpdater::defaultUpdateDefinitionUrl() {
    return QUrl(QString::fromLatin1(kDefaultDefinitionUrl));
}

QUrl AppUpdater::defaultProjectUrl() {
    return QUrl(QString::fromLatin1(kDefaultProjectUrl));
}

QString AppUpdater::updateChannel() {
    return QStringLiteral(TLM_UPDATE_CHANNEL);
}

int AppUpdater::compareVersions(const QString& left, const QString& right) {
    return compareSemanticVersions(left, right);
}

UpdateCheckResult AppUpdater::parseUpdatePayload(const QByteArray& payload,
                                                 const QString& currentVersion, QString* error) {
    if (error) {
        error->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        (!document.isObject() && !document.isArray())) {
        if (error) {
            *error = QObject::tr("The update response is not valid JSON.");
        }
        return {};
    }

    if (document.isArray()) {
        return parseGitHubReleaseList(document.array(), currentVersion, error);
    }

    const QJsonObject root = document.object();
    return root.contains(QStringLiteral("tag_name"))
               ? parseGitHubRelease(root, currentVersion, error)
               : parseDefinition(root, currentVersion, error);
}

bool AppUpdater::isChecking() const {
    return m_checkReply && m_checkReply->isRunning();
}

bool AppUpdater::isDownloading() const {
    return m_downloadReply && m_downloadReply->isRunning();
}

void AppUpdater::checkForUpdates(const QUrl& definitionUrl) {
    if (isChecking()) {
        Logger::debug(QStringLiteral("app.update.check.skip reason=already-running"));
        return;
    }

    const QUrl url = definitionUrl.isValid() ? definitionUrl : defaultUpdateDefinitionUrl();
    if (!isSecureWebUrl(url)) {
        const QString reason = tr("The update definition URL is not secure.");
        setState(m_hasUpdateAvailable ? UpdateState::Available : UpdateState::Error);
        emit checkFailed(reason);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TierListMaker/%1 (%2)")
                          .arg(QStringLiteral(TLM_APP_VERSION), updateChannel()));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kManifestTimeoutMs);

    m_checkPayload.clear();
    Logger::info(QStringLiteral("app.update.check.start channel=%1 url=\"%2\"")
                     .arg(updateChannel(), url.toString()));
    m_checkReply = m_network.get(request);
    connect(m_checkReply, &QIODevice::readyRead, this, [this]() {
        if (!m_checkReply) {
            return;
        }
        m_checkPayload.append(m_checkReply->readAll());
        if (m_checkPayload.size() > kMaximumManifestBytes) {
            m_checkReply->setProperty("tlmFailure", tr("The update response is too large."));
            m_checkReply->abort();
        }
    });
    connect(m_checkReply, &QNetworkReply::finished, this, &AppUpdater::finishCheckReply);
    setState(UpdateState::Checking);
    emit checkingStarted(url);
}

void AppUpdater::startUpdate() {
    if (m_state == UpdateState::Ready) {
        installDownloadedUpdate();
    } else if (m_state == UpdateState::Available || m_state == UpdateState::Error) {
        beginDownload();
    }
}

void AppUpdater::openUpdatePage(const UpdateCheckResult& result) {
    const UpdateCheckResult effective = result.latestVersion.isEmpty() ? m_lastResult : result;
    QUrl url = effective.openUrl.isValid() ? effective.openUrl : effective.downloadUrl;
    if (!url.isValid()) {
        url = defaultProjectUrl();
    }
    Logger::info(QStringLiteral("app.update.open url=\"%1\"").arg(url.toString()));
    QDesktopServices::openUrl(url);
}

void AppUpdater::finishCheckReply() {
    QNetworkReply* reply = m_checkReply;
    m_checkReply = nullptr;
    if (!reply) {
        return;
    }

    m_checkPayload.append(reply->readAll());
    const QUrl url = reply->url();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString failure = reply->property("tlmFailure").toString();
    if (failure.isEmpty() && reply->error() != QNetworkReply::NoError) {
        failure = reply->errorString();
    }
    reply->deleteLater();

    if (!failure.isEmpty()) {
        Logger::warn(QStringLiteral("app.update.check.failed url=\"%1\" status=%2 error=\"%3\"")
                         .arg(url.toString())
                         .arg(status)
                         .arg(failure));
        setState(m_hasUpdateAvailable ? UpdateState::Available : UpdateState::Error);
        emit checkFailed(failure);
        return;
    }

    QString parseError;
    UpdateCheckResult result =
        parseUpdatePayload(m_checkPayload, QStringLiteral(TLM_APP_VERSION), &parseError);
    m_checkPayload.clear();
    if (!parseError.isEmpty()) {
        Logger::warn(QStringLiteral("app.update.check.invalid url=\"%1\" status=%2 error=\"%3\"")
                         .arg(url.toString())
                         .arg(status)
                         .arg(parseError));
        setState(m_hasUpdateAvailable ? UpdateState::Available : UpdateState::Error);
        emit checkFailed(parseError);
        return;
    }

    Logger::info(QStringLiteral("app.update.check.finish current=%1 latest=%2 available=%3 "
                                "mandatory=%4 channel=%5")
                     .arg(result.currentVersion, result.latestVersion)
                     .arg(result.updateAvailable)
                     .arg(result.mandatory)
                     .arg(result.channel));
    m_lastResult = result;
    if (m_hasUpdateAvailable != result.updateAvailable) {
        m_hasUpdateAvailable = result.updateAvailable;
        emit updateNotificationChanged(m_hasUpdateAvailable);
    }
    if (result.updateAvailable) {
        setState(useCachedPackage(result) ? UpdateState::Ready : UpdateState::Available);
        emit updateAvailable(result);
    } else {
        m_downloadedPackagePath.clear();
        setState(UpdateState::Idle);
        emit noUpdateAvailable(result);
    }
}

void AppUpdater::beginDownload() {
    if (!m_lastResult.updateAvailable || isDownloading()) {
        return;
    }
    if (!m_lastResult.downloadUrl.isValid() || m_lastResult.sha256.size() != 64) {
        Logger::warn(QStringLiteral("app.update.download.skip reason=unverified-package"));
        openUpdatePage(m_lastResult);
        return;
    }
    if (useCachedPackage(m_lastResult)) {
        setState(UpdateState::Ready);
        emit updateReady(m_downloadedPackagePath);
        return;
    }

    const QString targetPath = packageCachePath(m_lastResult);
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        failDownload(tr("The update cache directory could not be created."));
        return;
    }
    m_downloadFile = std::make_unique<QSaveFile>(targetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        failDownload(m_downloadFile->errorString());
        return;
    }
    m_downloadHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_downloadedBytes = 0;

    QNetworkRequest request(m_lastResult.downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TierListMaker/%1 (%2)")
                          .arg(QStringLiteral(TLM_APP_VERSION), updateChannel()));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kPackageTimeoutMs);

    Logger::info(QStringLiteral("app.update.download.start version=%1 url=\"%2\"")
                     .arg(m_lastResult.latestVersion, m_lastResult.downloadUrl.toString()));
    m_downloadReply = m_network.get(request);
    connect(m_downloadReply, &QIODevice::readyRead, this, &AppUpdater::readDownloadData);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const qint64 expected = m_lastResult.packageSize > 0 ? m_lastResult.packageSize : total;
                emit downloadProgress(received, expected);
            });
    connect(m_downloadReply, &QNetworkReply::finished, this, &AppUpdater::finishDownloadReply);
    setState(UpdateState::Downloading);
    emit downloadProgress(0, m_lastResult.packageSize);
}

void AppUpdater::readDownloadData() {
    if (!m_downloadReply || !m_downloadFile || !m_downloadHash) {
        return;
    }
    const QByteArray data = m_downloadReply->readAll();
    if (data.isEmpty()) {
        return;
    }
    if (m_downloadedBytes + data.size() > kMaximumPackageBytes ||
        (m_lastResult.packageSize >= 0 &&
         m_downloadedBytes + data.size() > m_lastResult.packageSize)) {
        m_downloadReply->setProperty("tlmFailure", tr("The update package is larger than expected."));
        m_downloadReply->abort();
        return;
    }
    if (m_downloadFile->write(data) != data.size()) {
        m_downloadReply->setProperty("tlmFailure", m_downloadFile->errorString());
        m_downloadReply->abort();
        return;
    }
    m_downloadHash->addData(data);
    m_downloadedBytes += data.size();
}

void AppUpdater::finishDownloadReply() {
    QNetworkReply* reply = m_downloadReply;
    if (!reply) {
        return;
    }
    readDownloadData();
    m_downloadReply = nullptr;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString failure = reply->property("tlmFailure").toString();
    if (failure.isEmpty() && reply->error() != QNetworkReply::NoError) {
        failure = reply->errorString();
    }
    reply->deleteLater();
    if (failure.isEmpty() && status != 0 && (status < 200 || status >= 300)) {
        failure = tr("The update server returned HTTP %1.").arg(status);
    }
    if (failure.isEmpty() && m_lastResult.packageSize >= 0 &&
        m_downloadedBytes != m_lastResult.packageSize) {
        failure = tr("The downloaded update package has an unexpected size.");
    }
    if (failure.isEmpty() && m_downloadHash &&
        QString::fromLatin1(m_downloadHash->result().toHex()) != m_lastResult.sha256) {
        failure = tr("The downloaded update package failed checksum verification.");
    }
    if (!failure.isEmpty()) {
        failDownload(failure);
        return;
    }

    const QString targetPath = m_downloadFile->fileName();
    if (!m_downloadFile->commit()) {
        failDownload(m_downloadFile->errorString());
        return;
    }
    m_downloadFile.reset();
    m_downloadHash.reset();
    m_downloadedPackagePath = targetPath;
    Logger::info(QStringLiteral("app.update.download.ready version=%1 path=\"%2\"")
                     .arg(m_lastResult.latestVersion, targetPath));
    setState(UpdateState::Ready);
    emit updateReady(targetPath);
}

void AppUpdater::installDownloadedUpdate() {
    if (m_state != UpdateState::Ready || m_downloadedPackagePath.isEmpty() ||
        !verifyPackage(m_downloadedPackagePath, m_lastResult)) {
        failDownload(tr("The staged update package is no longer valid."));
        return;
    }

    setState(UpdateState::Installing);
    Logger::info(QStringLiteral("app.update.install.start path=\"%1\"")
                     .arg(m_downloadedPackagePath));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_downloadedPackagePath))) {
        const QString reason = tr("The update installer could not be opened.");
        setState(UpdateState::Ready);
        emit updateFailed(reason);
        return;
    }
#if defined(Q_OS_WIN)
    // Let ShellExecute finish creating the installer process before the app exits.
    QTimer::singleShot(750, QCoreApplication::instance(), []() { QCoreApplication::quit(); });
#endif
}

void AppUpdater::failDownload(const QString& reason) {
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
    }
    m_downloadFile.reset();
    m_downloadHash.reset();
    m_downloadedBytes = 0;
    Logger::warn(QStringLiteral("app.update.download.failed error=\"%1\"").arg(reason));
    setState(m_hasUpdateAvailable ? UpdateState::Available : UpdateState::Error);
    emit updateFailed(reason);
}

void AppUpdater::setState(UpdateState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

bool AppUpdater::useCachedPackage(const UpdateCheckResult& result) {
    if (!result.downloadUrl.isValid() || result.sha256.size() != 64 ||
        packageFileName(result).isEmpty()) {
        return false;
    }
    const QString path = packageCachePath(result);
    if (!QFileInfo(path).isFile() || !verifyPackage(path, result)) {
        return false;
    }
    m_downloadedPackagePath = path;
    return true;
}

} // namespace tlm
