#include "window/AppMessageDialog.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace tlm {

namespace {
QStyle::StandardPixmap standardPixmap(AppMessageDialog::Icon icon) {
    switch (icon) {
    case AppMessageDialog::Icon::Information:
        return QStyle::SP_MessageBoxInformation;
    case AppMessageDialog::Icon::Warning:
        return QStyle::SP_MessageBoxWarning;
    case AppMessageDialog::Icon::Critical:
        return QStyle::SP_MessageBoxCritical;
    }
    return QStyle::SP_MessageBoxInformation;
}
} // namespace

AppMessageDialog::AppMessageDialog(Icon icon, const QString& title, const QString& text,
                                   QDialogButtonBox::StandardButtons buttons, QWidget* parent)
    : AppDialog(title, parent), m_buttons(new QDialogButtonBox(buttons, this)) {
    setObjectName(QStringLiteral("AppMessageDialog"));
    setMinimumWidth(380);
    setMaximumWidth(600);

    auto* messageRow = new QHBoxLayout;
    messageRow->setContentsMargins(2, 4, 2, 4);
    messageRow->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(style()->standardIcon(standardPixmap(icon)).pixmap(32, 32));
    messageRow->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* messageLabel = new QLabel(text, this);
    messageLabel->setObjectName(QStringLiteral("AppMessageText"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setMinimumWidth(280);
    messageLabel->setMaximumWidth(500);
    messageRow->addWidget(messageLabel, 1, Qt::AlignVCenter);
    contentLayout()->addLayout(messageRow);
    contentLayout()->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        m_clickedButton = button;
        const auto role = m_buttons->buttonRole(button);
        if (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole) {
            AppDialog::reject();
        } else {
            AppDialog::accept();
        }
    });

    if (auto* cancel = m_buttons->button(QDialogButtonBox::Cancel)) {
        setEscapeButton(cancel);
    } else if (auto* no = m_buttons->button(QDialogButtonBox::No)) {
        setEscapeButton(no);
    }
}

QPushButton* AppMessageDialog::addButton(const QString& text,
                                         QDialogButtonBox::ButtonRole role) {
    return m_buttons->addButton(text, role);
}

QPushButton* AppMessageDialog::button(QDialogButtonBox::StandardButton button) const {
    return m_buttons->button(button);
}

void AppMessageDialog::setDefaultButton(QAbstractButton* button) {
    if (auto* pushButton = qobject_cast<QPushButton*>(button)) {
        pushButton->setDefault(true);
        pushButton->setFocus(Qt::OtherFocusReason);
    }
}

void AppMessageDialog::setDefaultButton(QDialogButtonBox::StandardButton button) {
    setDefaultButton(m_buttons->button(button));
}

void AppMessageDialog::setEscapeButton(QAbstractButton* button) {
    m_escapeButton = button;
}

void AppMessageDialog::setEscapeButton(QDialogButtonBox::StandardButton button) {
    setEscapeButton(m_buttons->button(button));
}

QAbstractButton* AppMessageDialog::clickedButton() const {
    return m_clickedButton;
}

QDialogButtonBox::StandardButton AppMessageDialog::clickedStandardButton() const {
    return m_clickedButton ? m_buttons->standardButton(m_clickedButton)
                           : QDialogButtonBox::NoButton;
}

QDialogButtonBox::StandardButton
AppMessageDialog::information(QWidget* parent, const QString& title, const QString& text,
                              QDialogButtonBox::StandardButtons buttons,
                              QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Information, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
AppMessageDialog::warning(QWidget* parent, const QString& title, const QString& text,
                          QDialogButtonBox::StandardButtons buttons,
                          QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Warning, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
AppMessageDialog::critical(QWidget* parent, const QString& title, const QString& text,
                           QDialogButtonBox::StandardButtons buttons,
                           QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Critical, parent, title, text, buttons, defaultButton);
}

void AppMessageDialog::reject() {
    if (!m_clickedButton) {
        m_clickedButton = m_escapeButton;
    }
    AppDialog::reject();
}

QDialogButtonBox::StandardButton
AppMessageDialog::run(Icon icon, QWidget* parent, const QString& title, const QString& text,
                      QDialogButtonBox::StandardButtons buttons,
                      QDialogButtonBox::StandardButton defaultButton) {
    AppMessageDialog dialog(icon, title, text, buttons, parent);
    dialog.setDefaultButton(defaultButton);
    dialog.exec();
    return dialog.clickedStandardButton();
}

} // namespace tlm
