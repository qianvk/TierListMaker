#include "widgets/EmptyStateWidget.h"

#include <QLabel>
#include <QVBoxLayout>

namespace tlm {

EmptyStateWidget::EmptyStateWidget(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto* subtitleLabel = new QLabel(subtitle, this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
}

} // namespace tlm

