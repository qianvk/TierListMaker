#pragma once

#include "window/AppDialog.h"

#include <QColor>

class QLineEdit;
class QPushButton;

namespace tlm {

/** Edits the visible tier label and color for existing or newly inserted rows. */
class TierRowEditDialog final : public AppDialog {
    Q_OBJECT

public:
    TierRowEditDialog(const QString& title, const QString& label, const QColor& color,
                      const QString& placeholder, QWidget* parent = nullptr);

    QString labelText() const;
    QColor color() const {
        return m_color;
    }

private:
    void applyColorButtonStyle();

    QLineEdit* m_labelEdit{nullptr};
    QPushButton* m_colorButton{nullptr};
    QColor m_color;
};

} // namespace tlm
