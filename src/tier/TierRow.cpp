#include "tier/TierRow.h"

#include <QUuid>

namespace tlm {

TierRow TierRow::makeDefault(QString label, QColor color, int order) {
    TierRow row;
    row.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    row.label = std::move(label);
    row.color = color;
    row.order = order;
    return row;
}

} // namespace tlm

