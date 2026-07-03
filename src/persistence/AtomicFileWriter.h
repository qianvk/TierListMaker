#pragma once

#include "persistence/Result.h"

#include <QByteArray>
#include <QString>

namespace tlm {

/** Writes files atomically through QSaveFile so interrupted saves do not corrupt projects. */
class AtomicFileWriter {
public:
    static Result<void> write(const QString& filePath, const QByteArray& data);
};

} // namespace tlm

