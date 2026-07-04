#include "tier/TierImage.h"

#include <QtGlobal>

namespace tlm {

namespace {
QRect centeredCropSourceRect(const QSize& sourceSize, const QSize& targetSize) {
    if (!sourceSize.isValid() || targetSize.isEmpty()) {
        return {};
    }

    const qreal targetRatio = static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
    const qreal sourceRatio = static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
    if (sourceRatio > targetRatio) {
        const int cropWidth = qRound(sourceSize.height() * targetRatio);
        return QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
    }
    const int cropHeight = qRound(sourceSize.width() / targetRatio);
    return QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(), cropHeight);
}
} // namespace

bool TierImage::hasCropRect() const {
    return cropRect.isValid() && cropRect.width() > 0.001 && cropRect.height() > 0.001;
}

QRect TierImage::thumbnailSourceRect(const QSize& sourceSize, const QSize& targetSize) const {
    if (!sourceSize.isValid()) {
        return {};
    }
    if (!hasCropRect()) {
        return centeredCropSourceRect(sourceSize, targetSize);
    }

    const qreal x = qBound<qreal>(0.0, cropRect.x(), 0.999);
    const qreal y = qBound<qreal>(0.0, cropRect.y(), 0.999);
    const qreal w = qBound<qreal>(0.001, cropRect.width(), 1.0 - x);
    const qreal h = qBound<qreal>(0.001, cropRect.height(), 1.0 - y);
    QRectF rectF(x * sourceSize.width(), y * sourceSize.height(),
                 w * sourceSize.width(), h * sourceSize.height());

    const qreal targetRatio = targetSize.isEmpty()
                                  ? 1.0
                                  : static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
    const qreal cropRatio = rectF.width() / qMax<qreal>(1.0, rectF.height());
    if (cropRatio > targetRatio) {
        const qreal nextWidth = rectF.height() * targetRatio;
        rectF.setLeft(rectF.center().x() - nextWidth / 2.0);
        rectF.setWidth(nextWidth);
    } else if (cropRatio < targetRatio) {
        const qreal nextHeight = rectF.width() / targetRatio;
        rectF.setTop(rectF.center().y() - nextHeight / 2.0);
        rectF.setHeight(nextHeight);
    }

    QRect rect = rectF.toAlignedRect();
    rect = rect.intersected(QRect(QPoint(0, 0), sourceSize));
    return rect.isValid() ? rect : centeredCropSourceRect(sourceSize, targetSize);
}

} // namespace tlm
