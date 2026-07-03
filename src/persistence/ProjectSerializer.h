#pragma once

#include "persistence/Result.h"
#include "tier/TierProject.h"

#include <QByteArray>

namespace tlm {

/** Converts TierProject instances to and from the stable `.tlmproject` JSON schema. */
class ProjectSerializer {
public:
    static constexpr int schemaVersion = 1;

    Result<QByteArray> serialize(const TierProject& project) const;
    Result<TierProject> deserialize(const QByteArray& data, const QString& filePath = {}) const;
};

} // namespace tlm

