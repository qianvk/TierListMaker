#include "persistence/RecentProjectsStore.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace tlm;

class tst_RecentProjectsStore : public QObject {
    Q_OBJECT

private slots:
    void addRenameRemove() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RecentProjectsStore store(dir.filePath(QStringLiteral("recent.json")));

        TierProject project = TierProject::createUntitled();
        project.filePath = dir.filePath(QStringLiteral("a.tlmproject"));
        project.name = QStringLiteral("A");
        QVERIFY(store.addOrUpdate(project));
        QCOMPARE(store.entries().size(), 1);

        QVERIFY(store.renameDisplayName(project.filePath, QStringLiteral("Renamed")));
        QCOMPARE(store.entries().first().name, QStringLiteral("Renamed"));

        QVERIFY(store.remove(project.filePath));
        QCOMPARE(store.entries().size(), 0);
    }
};

QTEST_MAIN(tst_RecentProjectsStore)
#include "tst_RecentProjectsStore.moc"

