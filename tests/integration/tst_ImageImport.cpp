#include "assets/AssetManager.h"
#include "persistence/ProjectRepository.h"

#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace tlm;

class tst_ImageImport : public QObject {
    Q_OBJECT

private slots:
    void importThenSaveOpenKeepsRelativeAssetPath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("image.png"));
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(source));

        TierProject project = TierProject::createUntitled();
        const QString projectPath = dir.filePath(QStringLiteral("import.tlmproject"));
        project.filePath = projectPath;

        AssetManager assets;
        auto imported = assets.importImages(project, {source}, ImageImportBehavior::CopyIntoProject);
        QVERIFY(imported);
        QVERIFY(!QFileInfo(project.images.first().importedAssetPath).isAbsolute());

        ProjectRepository repository;
        QVERIFY(repository.saveProject(project, projectPath));
        auto opened = repository.openProject(projectPath);
        QVERIFY(opened);
        QCOMPARE(opened.value().images.size(), 1);
        QVERIFY(!QFileInfo(opened.value().images.first().importedAssetPath).isAbsolute());
    }
};

QTEST_MAIN(tst_ImageImport)
#include "tst_ImageImport.moc"

