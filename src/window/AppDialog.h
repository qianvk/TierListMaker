#pragma once

#include <QDialog>

class QFrame;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace QWK {
class WidgetWindowAgent;
}

namespace tlm {

/** Modal app shell that preserves native platform window controls. */
class AppDialog : public QDialog {
    Q_OBJECT

public:
    explicit AppDialog(const QString& title, QWidget* parent = nullptr);

    bool isResizable() const;
    void setResizable(bool resizable);

    QWidget* titleBar() const {
        return m_titleBar;
    }
    QWidget* contentWidget() const {
        return m_content;
    }
    QVBoxLayout* contentLayout() const {
        return m_contentLayout;
    }

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi(const QString& title);
    void installWindowChrome();
    void refreshTitle();

    QFrame* m_surface{nullptr};
    QWidget* m_titleBar{nullptr};
    QLabel* m_titleLabel{nullptr};
    QToolButton* m_fallbackCloseButton{nullptr};
    QWidget* m_nativeButtonArea{nullptr};
    QWidget* m_nativeButtonSpacer{nullptr};
    QWidget* m_content{nullptr};
    QVBoxLayout* m_contentLayout{nullptr};
    QWK::WidgetWindowAgent* m_windowAgent{nullptr};
    bool m_resizable{false};
};

} // namespace tlm
