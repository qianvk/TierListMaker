#pragma once

#include <QRect>
#include <QSize>
#include <QWidget>

class QEvent;
class QGraphicsOpacityEffect;
class QHBoxLayout;
class QKeyEvent;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QToolButton;
class QVariantAnimation;
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
    void setSaveActionEnabled(bool enabled);
    void setResetRowsActionEnabled(bool enabled);
    void setTierFocusMode(bool enabled);
    void setLeadingReservedWidth(int width);
    void retranslateUi();
    QSize focusRevealSizeHint() const;
    QRect galleryButtonGlobalRect() const;

signals:
    void newRequested();
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void backgroundRequested(QWidget* anchor);
    void galleryRequested(QWidget* anchor);
    void resetRowsRequested();
    void tierFocusModeRequested();
    void projectTitleEdited(const QString& title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void submitTitleEdit(bool clearFocus);
    void cancelTitleEdit();
    void rememberTitleEditBaseline();
    QRect globalRectFor(QWidget* widget) const;
    void setToolbarReveal(qreal targetOpacity);
    void updateLayoutMargins();
    void updateTitleWidth();
    void updateTitleGeometry();
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    void installTitleEditOutsideClickFilter();
    void removeTitleEditOutsideClickFilter();
#endif

    QLineEdit* m_titleEdit{nullptr};
    QHBoxLayout* m_contentLayout{nullptr};
    QToolButton* m_newButton{nullptr};
    QToolButton* m_openButton{nullptr};
    QToolButton* m_saveButton{nullptr};
    QToolButton* m_backgroundButton{nullptr};
    QToolButton* m_galleryButton{nullptr};
    QToolButton* m_resetButton{nullptr};
    QToolButton* m_focusButton{nullptr};
    QWidget* m_buttonGroup{nullptr};
    QGraphicsOpacityEffect* m_buttonGroupOpacity{nullptr};
    QVariantAnimation* m_revealAnimation{nullptr};
    bool m_tierFocusMode{false};
    bool m_editorActionsVisible{true};
    bool m_titleEditable{true};
    bool m_submittingTitle{false};
    bool m_cancelingTitleEdit{false};
    int m_leadingReservedWidth{0};
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    bool m_titleEditOutsideClickFilterInstalled{false};
#endif
    QString m_titleEditBaseline;
};

} // namespace tlm
