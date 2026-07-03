#include <QFile>
#include <QtTest>

class tst_Resources : public QObject {
    Q_OBJECT

private slots:
    void iconsUsePublicAliases() {
        const QStringList icons = {QStringLiteral("plus.svg"),   QStringLiteral("folder.svg"),
                                   QStringLiteral("save.svg"),   QStringLiteral("import.svg"),
                                   QStringLiteral("export.svg"), QStringLiteral("edit.svg"),
                                   QStringLiteral("preferences.svg")};
        for (const QString& icon : icons) {
            QVERIFY2(QFile::exists(QStringLiteral(":/icons/") + icon), qPrintable(icon));
        }
    }
};

QTEST_MAIN(tst_Resources)
#include "tst_Resources.moc"
