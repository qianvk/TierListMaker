#pragma once

#include <QLabel>

namespace tlm {

/** Bold section label used to divide compact settings and editor groups. */
class SectionHeader : public QLabel {
    Q_OBJECT

public:
    explicit SectionHeader(const QString& text, QWidget* parent = nullptr);
};

} // namespace tlm

