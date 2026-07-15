#pragma once

#include "window/AppDialog.h"

#include <QDialogButtonBox>

class QAbstractButton;
class QPushButton;

namespace tlm {

/** Platform-ordered application prompt presented in the shared AppDialog chrome. */
class AppMessageDialog final : public AppDialog {
public:
    enum class Icon { Information, Warning, Critical };

    AppMessageDialog(Icon icon, const QString& title, const QString& text,
                     QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                     QWidget* parent = nullptr);

    QPushButton* addButton(const QString& text, QDialogButtonBox::ButtonRole role);
    QPushButton* button(QDialogButtonBox::StandardButton button) const;
    void setDefaultButton(QAbstractButton* button);
    void setDefaultButton(QDialogButtonBox::StandardButton button);
    void setEscapeButton(QAbstractButton* button);
    void setEscapeButton(QDialogButtonBox::StandardButton button);
    QAbstractButton* clickedButton() const;
    QDialogButtonBox::StandardButton clickedStandardButton() const;

    static QDialogButtonBox::StandardButton
    information(QWidget* parent, const QString& title, const QString& text,
                QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    warning(QWidget* parent, const QString& title, const QString& text,
            QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
            QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    critical(QWidget* parent, const QString& title, const QString& text,
             QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
             QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);

public slots:
    void reject() override;

private:
    static QDialogButtonBox::StandardButton
    run(Icon icon, QWidget* parent, const QString& title, const QString& text,
        QDialogButtonBox::StandardButtons buttons,
        QDialogButtonBox::StandardButton defaultButton);

    QDialogButtonBox* m_buttons{nullptr};
    QAbstractButton* m_clickedButton{nullptr};
    QAbstractButton* m_escapeButton{nullptr};
};

} // namespace tlm
