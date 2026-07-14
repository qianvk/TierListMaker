#pragma once

#include <QString>

class QWidget;

namespace tlm {

bool confirmDestructiveAction(QWidget* parent, const QString& title, const QString& text,
                              const QString& confirmText = {});

} // namespace tlm
