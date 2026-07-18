#pragma once

#include <QPixmap>
#include <QWidget>

class QParallelAnimationGroup;
class QPropertyAnimation;

namespace tlm {

enum class PreviewBackgroundMode;

/** Window-level animated image preview that owns input while it is visible. */
class PreviewOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QRect previewGeometry READ previewGeometry WRITE setPreviewGeometry)
    Q_PROPERTY(qreal backdropProgress READ backdropProgress WRITE setBackdropProgress)

public:
    explicit PreviewOverlay(QWidget* parent = nullptr);
    ~PreviewOverlay() override;

    QRect previewGeometry() const {
        return m_previewGeometry;
    }
    void setPreviewGeometry(const QRect& rect);

    qreal backdropProgress() const {
        return m_backdropProgress;
    }
    void setBackdropProgress(qreal progress);

    PreviewBackgroundMode backgroundMode() const {
        return m_backgroundMode;
    }
    void setBackgroundMode(PreviewBackgroundMode mode);

    bool toolTipsEnabled() const {
        return m_toolTipsEnabled;
    }
    void setToolTipsEnabled(bool enabled);

    bool isOpen() const {
        return m_open;
    }
    Q_INVOKABLE QString toolTipTextAt(QPoint position) const;
    void openPreview(const QRect& sourceRectInWindow, const QPixmap& pixmap);
    void closePreview();

signals:
    void opened();
    void closed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect targetRectForPixmap(const QPixmap& pixmap) const;
    void animateTo(const QRect& from, const QRect& to, qreal fromProgress, qreal toProgress,
                   bool closing);
    void rebuildProjectionCache();
    void setInputBarrierActive(bool active);
    bool isOverlayDispatchObject(const QObject* object) const;

    QPixmap m_pixmap;
    QPixmap m_projectionCache;
    QRect m_sourceGeometry;
    QRect m_previewGeometry;
    qreal m_backdropProgress{0.0};
    PreviewBackgroundMode m_backgroundMode;
    QParallelAnimationGroup* m_animationGroup{nullptr};
    QPropertyAnimation* m_geometryAnimation{nullptr};
    QPropertyAnimation* m_backdropAnimation{nullptr};
    bool m_open{false};
    bool m_closing{false};
    bool m_inputBarrierActive{false};
    bool m_toolTipsEnabled{true};
};

} // namespace tlm
