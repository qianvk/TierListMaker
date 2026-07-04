#pragma once

#include <vkframeless/WindowTitleBar.h>

#include <QRect>
#include <QSize>

class QEvent;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QLineEdit;
class QMouseEvent;
class QResizeEvent;
class QToolButton;
class QVariantAnimation;
class QWidget;

namespace tlm {

/** Frameless draggable title bar containing document title and primary commands. */
class AppTitleBar : public vkframeless::WindowTitleBar {
    Q_OBJECT

public:
    explicit AppTitleBar(QWidget* parent = nullptr);

    void setDocumentTitle(const QString& title);
    void setTitleEditable(bool editable);
    void setEditorActionsVisible(bool visible);
    void setSaveActionEnabled(bool enabled);
    void setResetRowsActionEnabled(bool enabled);
    void setTierFocusMode(bool enabled);
    void retranslateUi();
    QSize focusRevealSizeHint() const;
    QRect galleryButtonGlobalRect() const;

signals:
    void newRequested();
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void backgroundRequested(const QRect& globalButtonRect);
    void galleryRequested(const QRect& globalButtonRect);
    void resetRowsRequested();
    void tierFocusModeRequested();
    void projectTitleEdited(const QString& title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void submitTitleEdit(bool clearFocus);
    void cancelTitleEdit();
    void rememberTitleEditBaseline();
    QRect globalRectFor(QWidget* widget) const;
    void setToolbarReveal(qreal targetOpacity);
    void updateTitleWidth();
    void updateTitleGeometry();

    QLineEdit* m_titleEdit{nullptr};
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
    QString m_titleEditBaseline;
};

} // namespace tlm
