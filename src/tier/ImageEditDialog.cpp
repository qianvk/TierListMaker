#include "tier/ImageEditDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
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
                  center.y() - (pixelHeight / sourceH) / 2.0,
                  pixelWidth / sourceW, pixelHeight / sourceH);
    rect.moveLeft(qBound<qreal>(0.0, rect.left(), 1.0 - rect.width()));
    rect.moveTop(qBound<qreal>(0.0, rect.top(), 1.0 - rect.height()));
    return rect;
}
} // namespace

class CropEditorWidget final : public QWidget {
public:
    CropEditorWidget(const QPixmap& pixmap, QRectF cropRect, QSizeF aspectRatio,
                     QWidget* parent = nullptr)
        : QWidget(parent), m_pixmap(pixmap), m_aspectRatio(normalizedAspectRatio(aspectRatio)),
          m_cropRect(normalizedCrop(cropRect, pixmap.size(), m_aspectRatio)) {
        setMinimumSize(360, 360);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
    }

    QRectF cropRect() const {
        return normalizedCrop(m_cropRect, m_pixmap.size(), m_aspectRatio);
    }

protected:
    void paintEvent(QPaintEvent*) override {
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

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && cropFrame().contains(event->position())) {
            m_dragStart = event->pos();
            m_dragStartCrop = m_cropRect;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
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
        // The image moves under a fixed crop frame, so the sampled crop moves opposite to the
        // cursor.
        next.translate(-delta.x() * m_dragStartCrop.width() / frame.width(),
                       -delta.y() * m_dragStartCrop.height() / frame.height());
        m_cropRect = normalizedCrop(next, m_pixmap.size(), m_aspectRatio);
        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
        if (qFuzzyIsNull(steps)) {
            QWidget::wheelEvent(event);
            return;
        }
        const qreal factor = std::pow(0.88, steps);
        const QPointF center = m_cropRect.center();
        const qreal sourceW = qMax<qreal>(1.0, m_pixmap.width());
        const qreal sourceH = qMax<qreal>(1.0, m_pixmap.height());
        const qreal targetRatio = aspectValue(m_aspectRatio);
        const qreal pixelWidth = m_cropRect.width() * sourceW * factor;
        const qreal pixelHeight = pixelWidth / targetRatio;
        QRectF next(center.x() - (pixelWidth / sourceW) / 2.0,
                    center.y() - (pixelHeight / sourceH) / 2.0,
                    pixelWidth / sourceW, pixelHeight / sourceH);
        m_cropRect = normalizedCrop(next, m_pixmap.size(), m_aspectRatio);
        update();
        event->accept();
    }

private:
    QRectF cropFrame() const {
        const qreal targetRatio = aspectValue(m_aspectRatio);
        QSizeF frameSize(qMax(1, width() - 34), qMax(1, height() - 34));
        if (frameSize.width() / frameSize.height() > targetRatio) {
            frameSize.setWidth(frameSize.height() * targetRatio);
        } else {
            frameSize.setHeight(frameSize.width() / targetRatio);
        }
        return QRectF((width() - frameSize.width()) / 2.0,
                      (height() - frameSize.height()) / 2.0,
                      frameSize.width(), frameSize.height());
    }

    QRectF sourceRect() const {
        if (m_pixmap.isNull()) {
            return {};
        }
        const QRectF crop = normalizedCrop(m_cropRect, m_pixmap.size(), m_aspectRatio);
        return QRectF(crop.x() * m_pixmap.width(), crop.y() * m_pixmap.height(),
                      crop.width() * m_pixmap.width(), crop.height() * m_pixmap.height());
    }

    QPixmap m_pixmap;
    QSizeF m_aspectRatio;
    QRectF m_cropRect;
    QPoint m_dragStart;
    QRectF m_dragStartCrop;
};

ImageEditDialog::ImageEditDialog(const TierImage& image, const QPixmap& pixmap, QWidget* parent,
                                 QSizeF aspectRatio)
    : AppDialog(QObject::tr("Edit Image"), parent), m_nameEdit(new QLineEdit(this)),
      m_cropEditor(new CropEditorWidget(
          pixmap, image.hasCropRect() ? image.cropRect : defaultCropRect(pixmap.size(), aspectRatio),
          aspectRatio, this)) {
    setWindowTitle(tr("Edit Image"));
    setMinimumWidth(440);
    setObjectName(QStringLiteral("ImageEditDialog"));

    auto* layout = contentLayout();
    layout->setSpacing(14);

    auto* title = new QLabel(tr("Edit thumbnail crop"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    layout->addWidget(m_cropEditor, 1);

    m_nameEdit->setText(image.displayName.isEmpty() ? image.originalFileName : image.displayName);
    m_nameEdit->selectAll();
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("Name"), m_nameEdit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString ImageEditDialog::displayName() const {
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QRectF ImageEditDialog::cropRect() const {
    return m_cropEditor ? m_cropEditor->cropRect() : QRectF();
}

} // namespace tlm
