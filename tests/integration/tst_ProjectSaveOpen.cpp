#include "persistence/ProjectRepository.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace tlm;

class tst_ProjectSaveOpen : public QObject {
    Q_OBJECT

private slots:
    void saveOpenRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectRepository repository;
        TierProject project = TierProject::createUntitled();
        project.name = QStringLiteral("Round Trip");
        const QString path = dir.filePath(QStringLiteral("roundtrip.tlmproject"));
        QVERIFY(repository.saveProject(project, path));
        auto opened = repository.openProject(path);
        QVERIFY(opened);
        QCOMPARE(opened.value().name, QStringLiteral("Round Trip"));
        QCOMPARE(opened.value().filePath, QFileInfo(path).absoluteFilePath());
    }
};

QTEST_MAIN(tst_ProjectSaveOpen)
#include "tst_ProjectSaveOpen.moc"

