#include "assets/ImageLoader.h"

#include <QImageReader>

namespace tlm {

Result<QImage> ImageLoader::load(const QString& filePath, QSize targetSize) {
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    if (!targetSize.isEmpty()) {
        reader.setScaledSize(reader.size().scaled(targetSize, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return Result<QImage>::failure(QObject::tr("Could not load image."), reader.errorString());
    }
    return Result<QImage>::success(image);
}

QStringList ImageLoader::supportedNameFilters() {
    return {QObject::tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")};
}

} // namespace tlm
