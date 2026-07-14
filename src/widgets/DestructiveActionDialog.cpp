#include "widgets/DestructiveActionDialog.h"

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>

namespace tlm {

bool confirmDestructiveAction(QWidget* parent, const QString& title, const QString& text,
                              const QString& confirmText) {
    QMessageBox message(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
    QAbstractButton* confirmButton =
        confirmText.isEmpty()
            ? static_cast<QAbstractButton*>(message.addButton(QMessageBox::Yes))
            : static_cast<QAbstractButton*>(
                  message.addButton(confirmText, QMessageBox::DestructiveRole));
    QPushButton* cancelButton = message.addButton(QMessageBox::Cancel);

    // Destructive prompts intentionally require an explicit choice: Enter and Escape stay safe.
    message.setDefaultButton(cancelButton);
    message.setEscapeButton(cancelButton);
    message.exec();
    return message.clickedButton() == confirmButton;
}

} // namespace tlm
