#pragma once

#include <QColor>
#include <QString>

namespace tlm {

enum class ExportFormat { Png, Jpeg, Pdf };

/** User-selected export options for image/PDF rendering. */
class ExportOptions {
public:
    ExportFormat format{ExportFormat::Png};
    int scale{2};
    bool transparentBackground{false};
    bool includeTitle{true};
    QColor backgroundColor{QColor(QStringLiteral("#ffffff"))};

    static ExportFormat formatFromSuffix(const QString& suffix);
    static QString suffixForFormat(ExportFormat format);
};

} // namespace tlm

