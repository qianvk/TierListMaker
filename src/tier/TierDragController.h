#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "tier/TierImage.h"
#include "tier/TierProject.h"

#include <QFrame>
#include <QMimeData>

class QKeyEvent;

namespace tlm {

/** Shared MIME helpers for moving tier images through Qt drag-and-drop. */
class TierDragController {
public:
    static QString imageMimeType();
    static QString rowMimeType();
    static QMimeData* createMimeData(const QString& imageId);
    static QMimeData* createRowMimeData(const QString& rowId);
    static QString imageIdFromMimeData(const QMimeData* mimeData);
    static QString rowIdFromMimeData(const QMimeData* mimeData);
};

/** Small draggable thumbnail tile that emits the shared image MIME payload. */
class ImageTileWidget : public QFrame {
    Q_OBJECT

public:
    ImageTileWidget(const TierProject* project, const TierImage* image, const AssetManager* assetManager,
                    ThumbnailCache* thumbnailCache, QWidget* parent = nullptr);

    QString imageId() const { return m_imageId; }
    void setSelected(bool selected);
    void setTileExtent(int extent);
    QRect imageRectIn(const QWidget* ancestor) const;

signals:
    void selected(const QString& imageId);
    void previewRequested(const QString& imageId, const QRect& sourceRect);
    void dragActiveChanged(bool active);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    QString resolvedPath() const;
    QPixmap pixmap() const;

    const TierProject* m_project{nullptr};
    const TierImage* m_image{nullptr};
    const AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    QString m_imageId;
    QPoint m_dragStartPosition;
    int m_tileExtent{88};
    bool m_selected{false};
    bool m_dragInProgress{false};
};

} // namespace tlm
