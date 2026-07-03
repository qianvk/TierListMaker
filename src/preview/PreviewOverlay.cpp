#include "preview/PreviewOverlay.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

namespace tlm {

PreviewOverlay::PreviewOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void PreviewOverlay::setPreviewGeometry(const QRect& rect) {
    m_previewGeometry = rect;
    update();
}

void PreviewOverlay::setDimOpacity(qreal opacity) {
    m_dimOpacity = opacity;
    update();
}

void PreviewOverlay::openPreview(const QRect& sourceRectInWindow, const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return;
    }
    m_pixmap = pixmap;
    m_sourceGeometry = QRect(mapFrom(window(), sourceRectInWindow.topLeft()), sourceRectInWindow.size());
    m_open = true;
    setGeometry(parentWidget() ? parentWidget()->rect() : geometry());
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    animateTo(m_sourceGeometry, targetRectForPixmap(m_pixmap), 0.0, 0.34, false);
}

void PreviewOverlay::closePreview() {
    if (!m_open) {
        return;
    }
    animateTo(m_previewGeometry, m_sourceGeometry, m_dimOpacity, 0.0, true);
}

void PreviewOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), QColor(0, 0, 0, qRound(255.0 * m_dimOpacity)));
    if (m_pixmap.isNull() || m_previewGeometry.isEmpty()) {
        return;
    }
    QPainterPath path;
    path.addRoundedRect(m_previewGeometry, 16, 16);
    painter.setClipPath(path);
    painter.fillRect(m_previewGeometry, QColor(QStringLiteral("#111111")));
    painter.drawPixmap(m_previewGeometry, m_pixmap);
}

void PreviewOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_previewGeometry.contains(event->pos())) {
        closePreview();
    }
}

void PreviewOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        closePreview();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PreviewOverlay::resizeEvent(QResizeEvent*) {
    if (m_open && !m_pixmap.isNull()) {
        m_previewGeometry = targetRectForPixmap(m_pixmap);
    }
}

QRect PreviewOverlay::targetRectForPixmap(const QPixmap& pixmap) const {
    const QSize maxSize(qRound(width() * 0.8), qRound(height() * 0.8));
    const QSize scaled = pixmap.size().scaled(maxSize, Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
}

void PreviewOverlay::animateTo(const QRect& from, const QRect& to, qreal fromDim, qreal toDim,
                               bool closing) {
    auto* group = new QParallelAnimationGroup(this);
    auto* geometryAnimation = new QPropertyAnimation(this, "previewGeometry", group);
    geometryAnimation->setStartValue(from);
    geometryAnimation->setEndValue(to);
    geometryAnimation->setDuration(220);
    geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto* dimAnimation = new QPropertyAnimation(this, "dimOpacity", group);
    dimAnimation->setStartValue(fromDim);
    dimAnimation->setEndValue(toDim);
    dimAnimation->setDuration(180);
    dimAnimation->setEasingCurve(QEasingCurve::OutCubic);

    group->addAnimation(geometryAnimation);
    group->addAnimation(dimAnimation);
    connect(group, &QParallelAnimationGroup::finished, this, [this, group, closing]() {
        group->deleteLater();
        if (closing) {
            m_open = false;
            hide();
            m_pixmap = {};
            emit closed();
        }
    });
    group->start();
}

} // namespace tlm
