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
QRectF defaultCropRect(const QSize& size) {
    if (!size.isValid()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }
    if (size.width() > size.height()) {
        const qreal side = static_cast<qreal>(size.height()) / size.width();
        return QRectF((1.0 - side) / 2.0, 0.0, side, 1.0);
    }
    const qreal side = static_cast<qreal>(size.width()) / size.height();
    return QRectF(0.0, (1.0 - side) / 2.0, 1.0, side);
}

QRectF normalizedCrop(QRectF rect, const QSize& sourceSize) {
    if (!rect.isValid() || rect.width() <= 0.001 || rect.height() <= 0.001) {
        return defaultCropRect(sourceSize);
    }
    if (!sourceSize.isValid()) {
        rect = rect.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
        return rect.isValid() ? rect : QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal sourceW = qMax<qreal>(1.0, sourceSize.width());
    const qreal sourceH = qMax<qreal>(1.0, sourceSize.height());
    const QPointF center = rect.center();
    const qreal requestedPixelSide = qMin(rect.width() * sourceW, rect.height() * sourceH);
    const qreal maxPixelSide = qMin(sourceW, sourceH);
    const qreal pixelSide = qBound<qreal>(maxPixelSide * 0.08, requestedPixelSide, maxPixelSide);
    rect = QRectF(center.x() - (pixelSide / sourceW) / 2.0,
                  center.y() - (pixelSide / sourceH) / 2.0,
                  pixelSide / sourceW,
                  pixelSide / sourceH);
    rect.moveLeft(qBound<qreal>(0.0, rect.left(), 1.0 - rect.width()));
    rect.moveTop(qBound<qreal>(0.0, rect.top(), 1.0 - rect.height()));
    return rect;
}
} // namespace

class CropEditorWidget final : public QWidget {
public:
    CropEditorWidget(const QPixmap& pixmap, QRectF cropRect, QWidget* parent = nullptr)
        : QWidget(parent), m_pixmap(pixmap), m_cropRect(normalizedCrop(cropRect, pixmap.size())) {
        setMinimumSize(360, 360);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
    }

    QRectF cropRect() const { return normalizedCrop(m_cropRect, m_pixmap.size()); }

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
        // The image moves under a fixed crop frame, so the sampled crop moves opposite to the cursor.
        next.translate(-delta.x() * m_dragStartCrop.width() / frame.width(),
                       -delta.y() * m_dragStartCrop.height() / frame.height());
        m_cropRect = normalizedCrop(next, m_pixmap.size());
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
        qreal side = qBound<qreal>(0.08, m_cropRect.width() * factor, 1.0);
        QRectF next(center.x() - side / 2.0, center.y() - side / 2.0, side, side);
        m_cropRect = normalizedCrop(next, m_pixmap.size());
        update();
        event->accept();
    }

private:
    QRectF cropFrame() const {
        const int side = qMax(1, qMin(width(), height()) - 34);
        return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    }

    QRectF sourceRect() const {
        if (m_pixmap.isNull()) {
            return {};
        }
        const QRectF crop = normalizedCrop(m_cropRect, m_pixmap.size());
        return QRectF(crop.x() * m_pixmap.width(),
                      crop.y() * m_pixmap.height(),
                      crop.width() * m_pixmap.width(),
                      crop.height() * m_pixmap.height());
    }

    QPixmap m_pixmap;
    QRectF m_cropRect;
    QPoint m_dragStart;
    QRectF m_dragStartCrop;
};

ImageEditDialog::ImageEditDialog(const TierImage& image, const QPixmap& pixmap, QWidget* parent)
    : QDialog(parent),
      m_nameEdit(new QLineEdit(this)),
      m_cropEditor(new CropEditorWidget(pixmap, image.hasCropRect() ? image.cropRect : defaultCropRect(pixmap.size()),
                                        this)) {
    setWindowTitle(tr("Edit Image"));
    setModal(true);
    setMinimumWidth(440);
    setObjectName(QStringLiteral("ImageEditDialog"));
    setStyleSheet(QStringLiteral(
        "QDialog#ImageEditDialog{background:palette(base);}"
        "QLineEdit{background:palette(alternate-base);color:palette(window-text);"
        "border:1px solid palette(mid);border-radius:8px;padding:7px 9px;}"
        "QPushButton{border:1px solid palette(mid);border-radius:8px;padding:7px 14px;"
        "background:palette(alternate-base);color:palette(window-text);}"
        "QPushButton:hover{background:palette(midlight);}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
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
