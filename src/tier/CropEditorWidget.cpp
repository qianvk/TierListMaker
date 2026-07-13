#include "tier/CropEditorWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

namespace tlm {

namespace {
QSizeF normalizedAspectRatio(QSizeF ratio) {
    if (ratio.width() <= 0.0 || ratio.height() <= 0.0) {
        return QSizeF(1.0, 1.0);
    }
    return ratio;
}

qreal aspectValue(QSizeF ratio) {
    ratio = normalizedAspectRatio(ratio);
    return ratio.width() / ratio.height();
}

QRectF defaultCropRect(const QSize& size, QSizeF aspectRatio) {
    if (!size.isValid()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }
    const qreal targetRatio = aspectValue(aspectRatio);
    const qreal sourceRatio = static_cast<qreal>(size.width()) / qMax(1, size.height());
    if (sourceRatio > targetRatio) {
        const qreal width = static_cast<qreal>(size.height()) * targetRatio / size.width();
        return QRectF((1.0 - width) / 2.0, 0.0, width, 1.0);
    }
    const qreal height = static_cast<qreal>(size.width()) / targetRatio / size.height();
    return QRectF(0.0, (1.0 - height) / 2.0, 1.0, height);
}

QRectF normalizedCrop(QRectF rect, const QSize& sourceSize, QSizeF aspectRatio) {
    if (!rect.isValid() || rect.width() <= 0.001 || rect.height() <= 0.001) {
        return defaultCropRect(sourceSize, aspectRatio);
    }
    if (!sourceSize.isValid()) {
        rect = rect.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
        return rect.isValid() ? rect : QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal sourceW = qMax<qreal>(1.0, sourceSize.width());
    const qreal sourceH = qMax<qreal>(1.0, sourceSize.height());
    const QPointF center = rect.center();
    const qreal targetRatio = aspectValue(aspectRatio);
    qreal pixelWidth = rect.width() * sourceW;
    qreal pixelHeight = rect.height() * sourceH;
    if (pixelWidth / qMax<qreal>(1.0, pixelHeight) > targetRatio) {
        pixelWidth = pixelHeight * targetRatio;
    } else {
        pixelHeight = pixelWidth / targetRatio;
    }

    qreal maxWidth = sourceW;
    qreal maxHeight = sourceH;
    if (maxWidth / maxHeight > targetRatio) {
        maxWidth = maxHeight * targetRatio;
    } else {
        maxHeight = maxWidth / targetRatio;
    }
    pixelWidth = qBound<qreal>(maxWidth * 0.08, pixelWidth, maxWidth);
    pixelHeight = pixelWidth / targetRatio;
    if (pixelHeight > maxHeight) {
        pixelHeight = maxHeight;
        pixelWidth = pixelHeight * targetRatio;
    }

    rect = QRectF(center.x() - (pixelWidth / sourceW) / 2.0,
                  center.y() - (pixelHeight / sourceH) / 2.0, pixelWidth / sourceW,
                  pixelHeight / sourceH);
    rect.moveLeft(qBound<qreal>(0.0, rect.left(), 1.0 - rect.width()));
    rect.moveTop(qBound<qreal>(0.0, rect.top(), 1.0 - rect.height()));
    return rect;
}
} // namespace

CropEditorWidget::CropEditorWidget(const QPixmap& pixmap, QRectF cropRect, QSizeF aspectRatio,
                                   QWidget* parent)
    : QWidget(parent), m_pixmap(pixmap), m_aspectRatio(normalizedAspectRatio(aspectRatio)),
      m_cropRect(normalizedCrop(cropRect, pixmap.size(), m_aspectRatio)) {
    setMinimumSize(360, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
}

QRectF CropEditorWidget::cropRect() const {
    return normalizedCrop(m_cropRect, m_pixmap.size(), m_aspectRatio);
}

void CropEditorWidget::setPixmap(const QPixmap& pixmap, QRectF cropRect) {
    m_pixmap = pixmap;
    m_cropRect = normalizedCrop(cropRect, pixmap.size(), m_aspectRatio);
    update();
}

void CropEditorWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                           QPainter::TextAntialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    const QRectF frame = cropFrame();
    QPainterPath clip;
    clip.addRoundedRect(frame, 18, 18);
    painter.setClipPath(clip);
    painter.fillPath(clip, palette().color(QPalette::AlternateBase));
    if (!m_pixmap.isNull()) {
        painter.drawPixmap(frame, m_pixmap, sourceRect());
    }
    painter.setClipping(false);

    QColor outside = palette().color(QPalette::Base);
    outside.setAlpha(150);
    QPainterPath outsidePath;
    outsidePath.addRect(rect());
    outsidePath = outsidePath.subtracted(clip);
    painter.fillPath(outsidePath, outside);

    QColor stroke = palette().color(QPalette::Highlight);
    painter.setPen(QPen(stroke, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame.adjusted(1.0, 1.0, -1.0, -1.0), 18, 18);

    QColor grid = palette().color(QPalette::WindowText);
    grid.setAlpha(55);
    painter.setPen(QPen(grid, 1));
    for (int i = 1; i < 3; ++i) {
        const qreal x = frame.left() + frame.width() * i / 3.0;
        const qreal y = frame.top() + frame.height() * i / 3.0;
        painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
        painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    }
}

void CropEditorWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && cropFrame().contains(event->position())) {
        m_dragStart = event->pos();
        m_dragStartCrop = m_cropRect;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CropEditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QRectF frame = cropFrame();
    if (frame.width() <= 1.0 || frame.height() <= 1.0) {
        return;
    }

    const QPointF delta = event->pos() - m_dragStart;
    QRectF next = m_dragStartCrop;
    next.translate(-delta.x() * m_dragStartCrop.width() / frame.width(),
                   -delta.y() * m_dragStartCrop.height() / frame.height());
    m_cropRect = normalizedCrop(next, m_pixmap.size(), m_aspectRatio);
    update();
    event->accept();
}

void CropEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CropEditorWidget::wheelEvent(QWheelEvent* event) {
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (qFuzzyIsNull(steps)) {
        QWidget::wheelEvent(event);
        return;
    }
    const qreal factor = std::pow(0.88, steps);
    const QPointF center = m_cropRect.center();
    const qreal sourceW = qMax<qreal>(1.0, m_pixmap.width());
    const qreal targetRatio = aspectValue(m_aspectRatio);
    const qreal pixelWidth = m_cropRect.width() * sourceW * factor;
    const qreal pixelHeight = pixelWidth / targetRatio;
    QRectF next(center.x() - (pixelWidth / sourceW) / 2.0,
                center.y() - (pixelHeight / qMax<qreal>(1.0, m_pixmap.height())) / 2.0,
                pixelWidth / sourceW, pixelHeight / qMax<qreal>(1.0, m_pixmap.height()));
    m_cropRect = normalizedCrop(next, m_pixmap.size(), m_aspectRatio);
    update();
    event->accept();
}

QRectF CropEditorWidget::cropFrame() const {
    const qreal targetRatio = aspectValue(m_aspectRatio);
    QSizeF frameSize(qMax(1, width() - 34), qMax(1, height() - 34));
    if (frameSize.width() / frameSize.height() > targetRatio) {
        frameSize.setWidth(frameSize.height() * targetRatio);
    } else {
        frameSize.setHeight(frameSize.width() / targetRatio);
    }
    return QRectF((width() - frameSize.width()) / 2.0, (height() - frameSize.height()) / 2.0,
                  frameSize.width(), frameSize.height());
}

QRectF CropEditorWidget::sourceRect() const {
    if (m_pixmap.isNull()) {
        return {};
    }
    const QRectF crop = normalizedCrop(m_cropRect, m_pixmap.size(), m_aspectRatio);
    return QRectF(crop.x() * m_pixmap.width(), crop.y() * m_pixmap.height(),
                  crop.width() * m_pixmap.width(), crop.height() * m_pixmap.height());
}

} // namespace tlm
