#include "widgets/SearchField.h"

namespace tlm {

SearchField::SearchField(QWidget* parent) : QLineEdit(parent) {
    setClearButtonEnabled(true);
    setPlaceholderText(tr("Search"));
    setMinimumHeight(34);
    setStyleSheet(QStringLiteral(
        "QLineEdit{border:1px solid palette(mid);border-radius:8px;padding:6px 10px;"
        "background:palette(alternate-base);color:palette(window-text);}"
        "QLineEdit:focus{border-color:palette(highlight);}"));
}

} // namespace tlm
