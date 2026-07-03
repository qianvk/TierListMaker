#include "assets/AssetManager.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace tlm;

class tst_AssetManager : public QObject {
    Q_OBJECT

private slots:
    void importsAndMigratesSessionAsset() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(QImage(20, 20, QImage::Format_ARGB32_Premultiplied).save(source));

        TierProject project = TierProject::createUntitled();
        AssetManager assets;
        auto imported = assets.importImages(project, {source}, ImageImportBehavior::CopyIntoProject);
        QVERIFY(imported);
        QCOMPARE(project.images.size(), 1);
        QVERIFY(QFileInfo(project.images.first().importedAssetPath).isAbsolute());

        const QString projectPath = dir.filePath(QStringLiteral("project.tlmproject"));
        QVERIFY(assets.migrateSessionAssets(project, projectPath));
        QVERIFY(!QFileInfo(project.images.first().importedAssetPath).isAbsolute());
        QVERIFY(QFileInfo::exists(assets.resolvedImagePath(project, project.images.first())) ||
                QFileInfo::exists(QDir(QFileInfo(projectPath).absolutePath())
                                      .filePath(project.images.first().importedAssetPath)));
    }
};

QTEST_MAIN(tst_AssetManager)
#include "tst_AssetManager.moc"

