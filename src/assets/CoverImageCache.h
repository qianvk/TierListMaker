#pragma once

#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace tlm {

/** Caches a center-cropped image at the exact device-pixel size used for painting. */
class CoverImageCache final {
public:
    const QPixmap& pixmap(const QString& path, QSize logicalSize, qreal devicePixelRatio);
    void clear();

private:
    bool ensureSource(const QString& path);

    QString m_sourcePath;
    QImage m_source;
    QSize m_logicalSize;
    qreal m_devicePixelRatio{0.0};
    QPixmap m_rendered;
};

} // namespace tlm
