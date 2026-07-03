#pragma once

#include <QLineEdit>

namespace tlm {

/** Search line edit with a compact macOS-like rounded field style. */
class SearchField : public QLineEdit {
    Q_OBJECT

public:
    explicit SearchField(QWidget* parent = nullptr);
};

} // namespace tlm

