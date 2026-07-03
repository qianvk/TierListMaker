#pragma once

#include <QWidget>

namespace tlm {

/** Centered title/subtitle placeholder for empty pages and lists. */
class EmptyStateWidget : public QWidget {
    Q_OBJECT

public:
    explicit EmptyStateWidget(const QString& title, const QString& subtitle, QWidget* parent = nullptr);
};

} // namespace tlm

