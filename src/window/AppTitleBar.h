#pragma once

#include <QRect>
#include <QSize>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMoveEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QToolButton;
class QWidget;

namespace tlm {

/** Frameless draggable title bar containing document title and primary commands. */
class AppTitleBar : public QWidget {
    Q_OBJECT

public:
    explicit AppTitleBar(QWidget* parent = nullptr);
    ~AppTitleBar() override;

    void setDocumentTitle(const QString& title);
    void setTitleEditable(bool editable);
    void setEditorActionsVisible(bool visible);
    void setResetRowsActionEnabled(bool enabled);
    void setTierFocusMode(bool enabled);
    void setUnsavedIndicatorVisible(bool visible);
    void setLeadingReservedWidth(int width);
    void retranslateUi();
    QSize focusRevealSizeHint() const;
    QLineEdit* titleEditor() const;
    QList<QWidget*> interactiveWidgets() const;
    void raiseChrome();

signals:
    void templatesRequested(QWidget* anchor);
    void backgroundRequested(QWidget* anchor);
    void galleryRequested(QWidget* anchor);
    void resetRowsRequested();
    void tierFocusModeRequested();
    void projectTitleEdited(const QString& title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void submitTitleEdit(bool clearFocus);
    void cancelTitleEdit();
    void rememberTitleEditBaseline();
    void updateTitleWidth();
    void updateTitleGeometry();
    void setActionButtonsVisible(bool visible);
    int actionButtonsWidth() const;
    void installTitleEditOutsideClickFilter();
    void removeTitleEditOutsideClickFilter();

    QLineEdit* m_titleEdit{nullptr};
    QLabel* m_unsavedIndicator{nullptr};
    QToolButton* m_templatesButton{nullptr};
    QToolButton* m_backgroundButton{nullptr};
    QToolButton* m_galleryButton{nullptr};
    QToolButton* m_resetButton{nullptr};
    QToolButton* m_focusButton{nullptr};
    bool m_tierFocusMode{false};
    bool m_editorActionsVisible{true};
    bool m_titleEditable{true};
    bool m_submittingTitle{false};
    bool m_cancelingTitleEdit{false};
    int m_leadingReservedWidth{0};
    bool m_titleEditOutsideClickFilterInstalled{false};
    QString m_titleEditBaseline;
};

} // namespace tlm
