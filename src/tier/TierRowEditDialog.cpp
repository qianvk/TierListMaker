#include "tier/TierRowEditDialog.h"

#include "theme/Theme.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace tlm {

TierRowEditDialog::TierRowEditDialog(const QString& title, const QString& label,
                                     const QColor& color, const QString& placeholder,
                                     QWidget* parent)
    : AppDialog(title, parent), m_labelEdit(new QLineEdit(this)),
      m_colorButton(new QPushButton(this)), m_color(color.isValid() ? color : QColor("#8bdc8b")) {
    setObjectName(QStringLiteral("TierRowEditDialog"));
    setMinimumWidth(420);

    m_labelEdit->setText(label);
    m_labelEdit->setPlaceholderText(placeholder);
    m_labelEdit->setClearButtonEnabled(true);

    m_colorButton->setMinimumHeight(34);
    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        const QColor next = QColorDialog::getColor(m_color, this, tr("Tier Color"));
        if (!next.isValid()) {
            return;
        }
        m_color = next;
        applyColorButtonStyle();
    });
    applyColorButtonStyle();

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("Name"), m_labelEdit);
    form->addRow(tr("Color"), m_colorButton);
    contentLayout()->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    contentLayout()->addWidget(buttons);

    QTimer::singleShot(0, this, [this]() {
        m_labelEdit->setFocus(Qt::OtherFocusReason);
        m_labelEdit->setCursorPosition(m_labelEdit->text().size());
    });
}

QString TierRowEditDialog::labelText() const {
    return m_labelEdit ? m_labelEdit->text().trimmed() : QString();
}

void TierRowEditDialog::applyColorButtonStyle() {
    if (!m_colorButton) {
        return;
    }
    m_colorButton->setText(m_color.name(QColor::HexRgb));
    m_colorButton->setStyleSheet(
        QStringLiteral("QPushButton{background:%1;color:%2;}")
            .arg(m_color.name(QColor::HexRgb), contrastingTextColor(m_color).name(QColor::HexRgb)));
}

} // namespace tlm
