#include "widgets/DestructiveActionDialog.h"

#include "window/AppMessageDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>

namespace tlm {

bool confirmDestructiveAction(QWidget* parent, const QString& title, const QString& text,
                              const QString& confirmText) {
    AppMessageDialog message(AppMessageDialog::Icon::Warning, title, text,
                             QDialogButtonBox::Cancel, parent);
    QAbstractButton* confirmButton = message.addButton(
        confirmText.isEmpty() ? QCoreApplication::translate("DestructiveActionDialog", "Confirm")
                              : confirmText,
        QDialogButtonBox::DestructiveRole);
    QPushButton* cancelButton = message.button(QDialogButtonBox::Cancel);

    // Destructive prompts intentionally require an explicit choice: Enter and Escape stay safe.
    message.setDefaultButton(cancelButton);
    message.setEscapeButton(cancelButton);
    message.exec();
    return message.clickedButton() == confirmButton;
}

} // namespace tlm
