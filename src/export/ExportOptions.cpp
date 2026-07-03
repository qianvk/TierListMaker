#include "export/ExportOptions.h"

namespace tlm {

ExportFormat ExportOptions::formatFromSuffix(const QString& suffix) {
    const QString normalized = suffix.toLower();
    if (normalized == QStringLiteral("jpg") || normalized == QStringLiteral("jpeg")) {
        return ExportFormat::Jpeg;
    }
    if (normalized == QStringLiteral("pdf")) {
        return ExportFormat::Pdf;
    }
    return ExportFormat::Png;
}

QString ExportOptions::suffixForFormat(ExportFormat format) {
    switch (format) {
    case ExportFormat::Jpeg:
        return QStringLiteral("jpg");
    case ExportFormat::Pdf:
        return QStringLiteral("pdf");
    case ExportFormat::Png:
    default:
        return QStringLiteral("png");
    }
}

} // namespace tlm

