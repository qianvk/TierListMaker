#include "assets/AssetManager.h"
#include "persistence/ProjectRepository.h"
#include "persistence/RecentProjectsStore.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace tlm;

class tst_ProjectSaveAsFlow : public QObject {
    Q_OBJECT

private slots:
    void unsavedImportMigratesAssetsBeforeSave() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString source = dir.filePath(QStringLiteral("source.png"));
        QImage image(48, 48, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(source));

        TierProject project = TierProject::createUntitled();
        AssetManager assets;
        auto imported = assets.importImages(project, {source}, ImageImportBehavior::CopyIntoProject);
        QVERIFY(imported);
        QVERIFY(QFileInfo(project.images.first().importedAssetPath).isAbsolute());

        const QString projectPath = dir.filePath(QStringLiteral("saved.tlmproject"));
        project.filePath = QFileInfo(projectPath).absoluteFilePath();
        QVERIFY(assets.migrateSessionAssets(project, project.filePath));
        QVERIFY(!QFileInfo(project.images.first().importedAssetPath).isAbsolute());

        ProjectRepository repository;
        QVERIFY(repository.saveProject(project, project.filePath));

        RecentProjectsStore recent(dir.filePath(QStringLiteral("recent.json")));
        QVERIFY(recent.addOrUpdate(project));

        auto opened = repository.openProject(projectPath);
        QVERIFY(opened);
        QCOMPARE(opened.value().images.size(), 1);
        QVERIFY(QFileInfo::exists(assets.resolvedImagePath(opened.value(), opened.value().images.first())));
    }
};

QTEST_MAIN(tst_ProjectSaveAsFlow)
#include "tst_ProjectSaveAsFlow.moc"
