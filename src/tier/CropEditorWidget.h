#pragma once

#include <QPixmap>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace tlm {

/** Fixed-frame image crop editor shared by image and project-cover dialogs. */
class CropEditorWidget final : public QWidget {
public:
    CropEditorWidget(const QPixmap& pixmap, QRectF cropRect = {}, QSizeF aspectRatio = QSizeF(1.0, 1.0),
                     QWidget* parent = nullptr);

    QRectF cropRect() const;
    void setPixmap(const QPixmap& pixmap, QRectF cropRect = {});

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRectF cropFrame() const;
    QRectF sourceRect() const;

    QPixmap m_pixmap;
    QSizeF m_aspectRatio;
    QRectF m_cropRect;
    QPoint m_dragStart;
    QRectF m_dragStartCrop;
};

} // namespace tlm
