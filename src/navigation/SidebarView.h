#pragma once

#include <QListView>

namespace tlm {

/** Left navigation view configured for keyboard, mouse, and HiDPI sidebar use. */
class SidebarView : public QListView {
    Q_OBJECT

public:
    explicit SidebarView(QWidget* parent = nullptr);
};

} // namespace tlm

