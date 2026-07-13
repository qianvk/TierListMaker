#pragma once

#include "tier/TierImage.h"
#include "window/AppDialog.h"

#include <QPixmap>

class QLineEdit;

namespace tlm {

class CropEditorWidget;

/** Modal editor for image metadata and the square thumbnail crop used by tier/gallery tiles. */
class ImageEditDialog final : public AppDialog {
    Q_OBJECT

public:
    ImageEditDialog(const TierImage& image, const QPixmap& pixmap, QWidget* parent = nullptr);

    QString displayName() const;
    QRectF cropRect() const;

private:
    QLineEdit* m_nameEdit{nullptr};
    CropEditorWidget* m_cropEditor{nullptr};
};

} // namespace tlm
