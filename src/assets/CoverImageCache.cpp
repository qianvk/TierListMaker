#include "assets/CoverImageCache.h"

#include <QImageReader>

namespace tlm {

const QPixmap& CoverImageCache::pixmap(const QString& path, QSize logicalSize,
                                       qreal devicePixelRatio) {
    logicalSize = logicalSize.expandedTo(QSize(1, 1));
    devicePixelRatio = qMax<qreal>(1.0, devicePixelRatio);
    if (!ensureSource(path)) {
        return m_rendered;
    }
    if (!m_rendered.isNull() && m_logicalSize == logicalSize &&
        qFuzzyCompare(m_devicePixelRatio, devicePixelRatio)) {
        return m_rendered;
    }

    const QSize pixelSize(qMax(1, qCeil(logicalSize.width() * devicePixelRatio)),
                          qMax(1, qCeil(logicalSize.height() * devicePixelRatio)));
    const QImage scaled =
        m_source.scaled(pixelSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - pixelSize.width()) / 2,
                     (scaled.height() - pixelSize.height()) / 2, pixelSize.width(),
                     pixelSize.height());
    m_rendered = QPixmap::fromImage(scaled.copy(crop));
    m_rendered.setDevicePixelRatio(devicePixelRatio);
    m_logicalSize = logicalSize;
    m_devicePixelRatio = devicePixelRatio;
    return m_rendered;
}

void CoverImageCache::clear() {
    m_sourcePath.clear();
    m_source = {};
    m_logicalSize = {};
    m_devicePixelRatio = 0.0;
    m_rendered = {};
}

bool CoverImageCache::ensureSource(const QString& path) {
    if (path == m_sourcePath) {
        return !m_source.isNull();
    }

    clear();
    m_sourcePath = path;
    if (path.isEmpty()) {
        return false;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    m_source = reader.read();
    return !m_source.isNull();
}

} // namespace tlm
