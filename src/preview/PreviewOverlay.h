#pragma once

#include <QPixmap>
#include <QWidget>

namespace tlm {

/** Animated dimming overlay that previews an image from its source tile rectangle. */
class PreviewOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QRect previewGeometry READ previewGeometry WRITE setPreviewGeometry)
    Q_PROPERTY(qreal dimOpacity READ dimOpacity WRITE setDimOpacity)

public:
    explicit PreviewOverlay(QWidget* parent = nullptr);

    QRect previewGeometry() const { return m_previewGeometry; }
    void setPreviewGeometry(const QRect& rect);

    qreal dimOpacity() const { return m_dimOpacity; }
    void setDimOpacity(qreal opacity);

    bool isOpen() const { return m_open; }
    void openPreview(const QRect& sourceRectInWindow, const QPixmap& pixmap);
    void closePreview();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect targetRectForPixmap(const QPixmap& pixmap) const;
    void animateTo(const QRect& from, const QRect& to, qreal fromDim, qreal toDim, bool closing);

    QPixmap m_pixmap;
    QRect m_sourceGeometry;
    QRect m_previewGeometry;
    qreal m_dimOpacity{0.0};
    bool m_open{false};
};

} // namespace tlm

