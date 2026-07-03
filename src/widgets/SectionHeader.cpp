#include "widgets/SectionHeader.h"

namespace tlm {

SectionHeader::SectionHeader(const QString& text, QWidget* parent) : QLabel(text, parent) {
    QFont f = font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    setFont(f);
}

} // namespace tlm

