#pragma once

#include "persistence/Result.h"

#include <QImage>
#include <QString>

namespace tlm {

/** Loads images with Qt image plugins and reports unsupported/corrupt files clearly. */
class ImageLoader {
public:
    static Result<QImage> load(const QString& filePath, QSize targetSize = {});
    static QStringList supportedNameFilters();
};

} // namespace tlm
