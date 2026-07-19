#include "update/AppUpdater.h"

#include <QtTest>

using namespace tlm;

class AppUpdaterTest final : public QObject {
    Q_OBJECT

private slots:
    void comparesSemanticVersions_data();
    void comparesSemanticVersions();
    void parsesPlatformManifest();
    void selectsLocalizedChangelog();
    void parsesGitHubReleaseFeed();
    void requiresSecurePackageMetadata();
};

void AppUpdaterTest::comparesSemanticVersions_data() {
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::addColumn<int>("expectedSign");

    QTest::newRow("equal") << QStringLiteral("1.2.3") << QStringLiteral("v1.2.3") << 0;
    QTest::newRow("patch") << QStringLiteral("1.2.3") << QStringLiteral("1.2.4") << -1;
    QTest::newRow("stable-after-beta")
        << QStringLiteral("1.0.0") << QStringLiteral("1.0.0-beta.9") << 1;
    QTest::newRow("numeric-prerelease")
        << QStringLiteral("1.0.0-beta.10") << QStringLiteral("1.0.0-beta.2") << 1;
    QTest::newRow("shorter-prerelease")
        << QStringLiteral("1.0.0-beta") << QStringLiteral("1.0.0-beta.1") << -1;
    QTest::newRow("build-metadata")
        << QStringLiteral("1.0.0+build.1") << QStringLiteral("1.0.0+build.2") << 0;
}

void AppUpdaterTest::selectsLocalizedChangelog() {
    const QByteArray manifest = R"json(
{
  "channel": "beta",
  "localizations": {
    "en": { "changelog": "English release notes" },
    "zh_CN": { "changelog": "Chinese release notes" }
  },
  "updates": {
    "default": {
      "latest-version": "0.2.0-beta.2"
    }
  }
}
)json";

    QString error;
    const UpdateCheckResult chinese = AppUpdater::parseUpdatePayload(
        manifest, QStringLiteral("0.2.0-beta.1"), &error, QStringLiteral("zh_CN"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(chinese.changelog, QStringLiteral("Chinese release notes"));

    const UpdateCheckResult english = AppUpdater::parseUpdatePayload(
        manifest, QStringLiteral("0.2.0-beta.1"), &error, QStringLiteral("en"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(english.changelog, QStringLiteral("English release notes"));
}

void AppUpdaterTest::comparesSemanticVersions() {
    QFETCH(QString, left);
    QFETCH(QString, right);
    QFETCH(int, expectedSign);
    const int comparison = AppUpdater::compareVersions(left, right);
    QCOMPARE(comparison == 0 ? 0 : (comparison < 0 ? -1 : 1), expectedSign);
}

void AppUpdaterTest::parsesPlatformManifest() {
    const QByteArray manifest = R"json(
{
  "schema-version": 2,
  "channel": "beta",
  "updates": {
    "windows": {
      "latest-version": "0.2.0-beta.2",
      "runtime-version": "qt-6.10.1-r1",
      "minimum-supported-version": "0.2.0",
      "download-url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.2/TierListMaker-0.2.0-beta.2-Windows-AMD64.exe",
      "release-url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.2",
      "file-name": "TierListMaker-0.2.0-beta.2-Windows-AMD64.exe",
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "size": 1234,
      "changelog": "Beta fixes",
      "update": {
        "download-url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.2/TierListMaker-0.2.0-beta.2-WinUpdate-AMD64.exe",
        "file-name": "TierListMaker-0.2.0-beta.2-WinUpdate-AMD64.exe",
        "sha256": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        "size": 567
      }
    },
    "macos": {
      "latest-version": "0.2.0-beta.2",
      "minimum-supported-version": "0.2.0",
      "download-url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.2/TierListMaker-0.2.0-beta.2-Darwin-arm64.dmg",
      "release-url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.2",
      "file-name": "TierListMaker-0.2.0-beta.2-Darwin-arm64.dmg",
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "size": 1234
    },
    "linux": {
      "latest-version": "0.2.0-beta.2",
      "minimum-supported-version": "0.2.0",
      "download-url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.2/TierListMaker-0.2.0-beta.2-Linux-x86_64.AppImage",
      "release-url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.2",
      "file-name": "TierListMaker-0.2.0-beta.2-Linux-x86_64.AppImage",
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "size": 1234
    }
  }
}
)json";

    QString error;
    const UpdateCheckResult result =
        AppUpdater::parseUpdatePayload(manifest, QStringLiteral("0.2.0-beta.1"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(result.updateAvailable);
    QVERIFY(result.mandatory);
    QCOMPARE(result.latestVersion, QStringLiteral("0.2.0-beta.2"));
    QCOMPARE(result.channel, QStringLiteral("beta"));
    QCOMPARE(result.sha256.size(), 64);
    QVERIFY(result.downloadUrl.isValid());
#if defined(Q_OS_WIN)
    QCOMPARE(AppUpdater::runtimeVersion(), QStringLiteral("qt-6.10.1-r1"));
    QVERIFY(result.lightweightPackage);
    QCOMPARE(result.fileName, QStringLiteral("TierListMaker-0.2.0-beta.2-WinUpdate-AMD64.exe"));
    QCOMPARE(result.packageSize, 567);

    QByteArray incompatibleManifest = manifest;
    incompatibleManifest.replace("qt-6.10.1-r1", "qt-6.10.1-r2");
    const UpdateCheckResult fallbackResult =
        AppUpdater::parseUpdatePayload(incompatibleManifest,
                                       QStringLiteral("0.2.0-beta.1"),
                                       &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!fallbackResult.lightweightPackage);
    QCOMPARE(fallbackResult.fileName,
             QStringLiteral("TierListMaker-0.2.0-beta.2-Windows-AMD64.exe"));
    QCOMPARE(fallbackResult.packageSize, 1234);
#else
    QVERIFY(!result.lightweightPackage);
    QCOMPARE(result.packageSize, 1234);
#endif
}

void AppUpdaterTest::parsesGitHubReleaseFeed() {
#if defined(Q_OS_WIN)
    constexpr auto packageName = "TierListMaker-0.2.0-beta.3-Windows-AMD64.exe";
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    constexpr auto packageName = "TierListMaker-0.2.0-beta.3-Darwin-universal.dmg";
#else
    constexpr auto packageName = "TierListMaker-0.2.0-beta.3-Linux-x86_64.AppImage";
#endif
    const QByteArray releaseTemplate = R"json(
[
  {
    "tag_name": "v0.2.0-beta.4",
    "html_url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.4",
    "draft": true,
    "prerelease": true,
    "assets": []
  },
  {
    "tag_name": "v0.2.0-beta.3",
    "html_url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.3",
    "body": "Release notes",
    "draft": false,
    "prerelease": true,
    "assets": [
      {
        "name": "%1",
        "browser_download_url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.3/%1",
        "size": 4321,
        "digest": "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
      },
      {
        "name": "updates.json",
        "browser_download_url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.3/updates.json",
        "size": 1024
      },
      {
        "name": "TierListMaker-0.2.0-beta.3-WinUpdate-AMD64.exe",
        "browser_download_url": "https://github.com/qianvk/TierListMaker/releases/download/v0.2.0-beta.3/TierListMaker-0.2.0-beta.3-WinUpdate-AMD64.exe",
        "size": 678,
        "digest": "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
      }
    ]
  },
  {
    "tag_name": "v0.2.0-beta.2",
    "html_url": "https://github.com/qianvk/TierListMaker/releases/tag/v0.2.0-beta.2",
    "draft": false,
    "prerelease": true,
    "assets": []
  }
]
)json";
    const QByteArray payload =
        QString::fromUtf8(releaseTemplate).arg(QString::fromLatin1(packageName)).toUtf8();

    QString error;
    const UpdateCheckResult result =
        AppUpdater::parseUpdatePayload(payload, QStringLiteral("0.2.0-beta.1"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(result.updateAvailable);
    QCOMPARE(result.latestVersion, QStringLiteral("0.2.0-beta.3"));
    QCOMPARE(result.channel, QStringLiteral("beta"));
    QCOMPARE(result.fileName, QString::fromLatin1(packageName));
    QCOMPARE(result.packageSize, 4321);
    QCOMPARE(result.sha256,
             QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    QCOMPARE(result.metadataUrl,
             QUrl(QStringLiteral("https://github.com/qianvk/TierListMaker/releases/download/"
                                 "v0.2.0-beta.3/updates.json")));
#if defined(Q_OS_WIN)
    QCOMPARE(result.updateFileName,
             QStringLiteral("TierListMaker-0.2.0-beta.3-WinUpdate-AMD64.exe"));
    QCOMPARE(result.updatePackageSize, 678);
    QCOMPARE(result.updateSha256,
             QStringLiteral("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"));
#endif
}

void AppUpdaterTest::requiresSecurePackageMetadata() {
    const QByteArray manifest = R"json(
{
  "updates": {
    "default": {
      "latest-version": "9.0.0",
      "download-url": "http://example.com/TierListMaker.exe",
      "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
  }
}
)json";

    QString error;
    const UpdateCheckResult result =
        AppUpdater::parseUpdatePayload(manifest, QStringLiteral("0.2.0"), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!result.updateAvailable);
}

QTEST_MAIN(AppUpdaterTest)

#include "tst_AppUpdater.moc"
