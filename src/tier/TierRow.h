#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace tlm {

/** Persisted row definition for one rank in a tier-list project. */
class TierRow {
public:
    QString id;
    QString label;
    QColor color;
    int order{0};
    int height{88};
    QStringList imageIds;

    static TierRow makeDefault(QString label, QColor color, int order);
};

} // namespace tlm

