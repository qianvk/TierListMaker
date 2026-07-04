#include "persistence/ProjectSerializer.h"

#include <QtTest>

using namespace tlm;

class tst_ProjectSerializer : public QObject {
    Q_OBJECT

private slots:
    void roundTripDefaultProject() {
        TierProject project = TierProject::createUntitled();
        project.name = QStringLiteral("Serializer Test");
        project.rows.first().imageIds.append(QStringLiteral("missing-ok"));

        ProjectSerializer serializer;
        auto data = serializer.serialize(project);
        QVERIFY(data);
        auto loaded = serializer.deserialize(data.value(), QStringLiteral("/tmp/test.tlmproject"));
        QVERIFY(loaded);
        QCOMPARE(loaded.value().name, project.name);
        QCOMPARE(loaded.value().rows.size(), 5);
        QVERIFY(!loaded.value().dirty);
    }

    void rejectsMalformedJson() {
        ProjectSerializer serializer;
        auto loaded = serializer.deserialize(QByteArray("{not json"));
        QVERIFY(!loaded);
        QVERIFY(!loaded.error().message.isEmpty());
    }

    void rejectsUnsupportedSchema() {
        ProjectSerializer serializer;
        auto loaded = serializer.deserialize(QByteArray(R"({"schemaVersion":99})"));
        QVERIFY(!loaded);
    }

    void preservesTierAndImageOrdering() {
        TierProject project = TierProject::createUntitled();
        TierRow moved = project.rows.takeAt(3);
        project.rows.insert(1, moved);
        for (int i = 0; i < project.rows.size(); ++i) {
            project.rows[i].order = i;
        }

        TierImage alpha;
        alpha.id = QStringLiteral("image-alpha");
        alpha.displayName = QStringLiteral("Alpha");
        alpha.cropRect = QRectF(0.125, 0.25, 0.5, 0.5);
        TierImage beta;
        beta.id = QStringLiteral("image-beta");
        beta.displayName = QStringLiteral("Beta");
        TierImage gamma;
        gamma.id = QStringLiteral("image-gamma");
        gamma.displayName = QStringLiteral("Gamma");
        project.images = {alpha, beta, gamma};

        const QString rowId = project.rows.at(1).id;
        project.rows[1].imageIds = {QStringLiteral("image-gamma"), QStringLiteral("image-alpha")};
        project.normalizeOrdering();

        ProjectSerializer serializer;
        auto data = serializer.serialize(project);
        QVERIFY(data);
        auto loaded = serializer.deserialize(data.value(), QStringLiteral("/tmp/ordered.tlmproject"));
        QVERIFY(loaded);

        QCOMPARE(loaded.value().rows.at(1).id, rowId);
        QCOMPARE(loaded.value().rows.at(1).imageIds,
                 QStringList({QStringLiteral("image-gamma"), QStringLiteral("image-alpha")}));
        const TierImage* gammaImage = loaded.value().imageById(QStringLiteral("image-gamma"));
        QVERIFY(gammaImage);
        QVERIFY(gammaImage->assignedTierRowId.has_value());
        QCOMPARE(*gammaImage->assignedTierRowId, rowId);
        QCOMPARE(gammaImage->order, 0);
        const TierImage* alphaImage = loaded.value().imageById(QStringLiteral("image-alpha"));
        QVERIFY(alphaImage);
        QVERIFY(alphaImage->hasCropRect());
        QCOMPARE(alphaImage->cropRect, QRectF(0.125, 0.25, 0.5, 0.5));
    }
};

QTEST_MAIN(tst_ProjectSerializer)
#include "tst_ProjectSerializer.moc"
