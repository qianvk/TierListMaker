#pragma once

#include <QListView>

namespace tlm {

/** Left navigation view configured for keyboard, mouse, and HiDPI sidebar use. */
class SidebarView : public QListView {
    Q_OBJECT

public:
    explicit SidebarView(QWidget* parent = nullptr);

    int preferredWidth() const;
    int availableWidth() const {
        return m_availableWidth;
    }
    void setAvailableWidth(int width);

private:
    int m_availableWidth{208};
};

} // namespace tlm
