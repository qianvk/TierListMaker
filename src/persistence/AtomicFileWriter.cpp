#include "persistence/AtomicFileWriter.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace tlm {

Result<void> AtomicFileWriter::write(const QString& filePath, const QByteArray& data) {
    const QFileInfo info(filePath);
    if (info.fileName().isEmpty()) {
        return Result<void>::failure(QObject::tr("Invalid file path."));
    }
    if (!QDir().mkpath(info.absolutePath())) {
        return Result<void>::failure(QObject::tr("Could not create the destination folder."),
                                     info.absolutePath());
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<void>::failure(QObject::tr("Could not open the file for writing."),
                                     file.errorString());
    }
    if (file.write(data) != data.size()) {
        return Result<void>::failure(QObject::tr("Could not write the complete file."),
                                     file.errorString());
    }
    if (!file.commit()) {
        return Result<void>::failure(QObject::tr("Could not commit the file."),
                                     file.errorString());
    }
    return Result<void>::success();
}

} // namespace tlm

