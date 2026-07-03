#include "window/AppTitleBar.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

namespace tlm {

namespace {
QToolButton* makeButton(const QString& text, const QString& iconPath, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setToolTip(text);
    button->setIcon(QIcon(iconPath));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(false);
    button->setFixedSize(32, 32);
    button->setIconSize(QSize(19, 19));
    button->setStyleSheet(QStringLiteral(
        "QToolButton{border:none;border-radius:8px;background:rgba(255,255,255,0);}"
        "QToolButton:hover{background:rgba(60,80,110,28);}"
        "QToolButton:pressed{background:rgba(60,80,110,48);}"
        "QToolButton:disabled{background:transparent;color:palette(mid);}"
        "QToolButton:disabled:hover{background:transparent;}"));
    return button;
}
} // namespace

AppTitleBar::AppTitleBar(QWidget* parent) : vkframeless::WindowTitleBar(parent) {
    setPreferredHeight(54);
    setBottomSeparatorVisible(false);
    setBackgroundOpacity(0.0);
    setBackgroundColor(palette().color(QPalette::Base));

    auto* layout = contentLayout();
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(6);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("ProjectTitleEdit"));
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setPlaceholderText(tr("Untitled Tier List"));
    m_titleEdit->setFocusPolicy(Qt::ClickFocus);
    QFont titleFont = m_titleEdit->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 5);
    m_titleEdit->setFont(titleFont);
    m_titleEdit->setMinimumWidth(160);
    m_titleEdit->setMaximumWidth(520);
    m_titleEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_titleEdit->installEventFilter(this);
    m_titleEdit->setStyleSheet(QStringLiteral(
        "QLineEdit#ProjectTitleEdit{background:transparent;border:none;"
        "border-radius:0px;padding:4px 10px;color:palette(window-text);selection-background-color:#7aa2f7;"
        "selection-color:white;}"));
    layout->addStretch(1);

    m_newButton = makeButton(tr("New"), QStringLiteral(":/icons/plus.svg"), this);
    m_openButton = makeButton(tr("Open"), QStringLiteral(":/icons/folder.svg"), this);
    m_saveButton = makeButton(tr("Save"), QStringLiteral(":/icons/save.svg"), this);
    m_backgroundButton = makeButton(tr("Background"), QStringLiteral(":/icons/image.svg"), this);
    m_resetButton = makeButton(tr("Reset Rows"), QStringLiteral(":/icons/reset.svg"), this);
    m_focusButton = makeButton(tr("Enter Tier Focus"), QStringLiteral(":/icons/focus.svg"), this);
    m_buttonGroup = new QWidget(this);
    m_buttonGroup->setObjectName(QStringLiteral("ToolbarButtonGroup"));
    m_buttonGroup->setMouseTracking(true);
    m_buttonGroup->installEventFilter(this);
    m_buttonGroup->setStyleSheet(QStringLiteral("QWidget#ToolbarButtonGroup{background:transparent;border:none;}"));
    m_buttonGroupOpacity = new QGraphicsOpacityEffect(m_buttonGroup);
    m_buttonGroupOpacity->setOpacity(1.0);
    m_buttonGroup->setGraphicsEffect(m_buttonGroupOpacity);
    auto* buttonsLayout = new QHBoxLayout(m_buttonGroup);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(2);
    for (QToolButton* button : {m_newButton, m_openButton, m_saveButton, m_backgroundButton, m_resetButton,
                                m_focusButton}) {
        button->installEventFilter(this);
        buttonsLayout->addWidget(button);
    }
    layout->addWidget(m_buttonGroup);

    connect(m_newButton, &QToolButton::clicked, this, &AppTitleBar::newRequested);
    connect(m_openButton, &QToolButton::clicked, this, &AppTitleBar::openRequested);
    connect(m_saveButton, &QToolButton::clicked, this, &AppTitleBar::saveRequested);
    connect(m_backgroundButton, &QToolButton::clicked, this,
            [this]() { emit backgroundRequested(globalRectFor(m_backgroundButton)); });
    connect(m_resetButton, &QToolButton::clicked, this, &AppTitleBar::resetRowsRequested);
    connect(m_focusButton, &QToolButton::clicked, this, &AppTitleBar::tierFocusModeRequested);
    connect(m_titleEdit, &QLineEdit::textChanged, this, &AppTitleBar::updateTitleWidth);

    auto* cancelShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_titleEdit);
    cancelShortcut->setContext(Qt::WidgetShortcut);
    connect(cancelShortcut, &QShortcut::activated, this, &AppTitleBar::cancelTitleEdit);
    auto* commitShortcut = new QShortcut(QKeySequence(Qt::Key_Return), m_titleEdit);
    commitShortcut->setContext(Qt::WidgetShortcut);
    connect(commitShortcut, &QShortcut::activated, this, [this]() { submitTitleEdit(true); });
    auto* keypadCommitShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), m_titleEdit);
    keypadCommitShortcut->setContext(Qt::WidgetShortcut);
    connect(keypadCommitShortcut, &QShortcut::activated, this, [this]() { submitTitleEdit(true); });

    rememberTitleEditBaseline();
    updateTitleGeometry();
}

void AppTitleBar::retranslateUi() {
    if (m_titleEdit) {
        m_titleEdit->setPlaceholderText(tr("Untitled Tier List"));
    }
    if (m_newButton) {
        m_newButton->setToolTip(tr("New"));
    }
    if (m_openButton) {
        m_openButton->setToolTip(tr("Open"));
    }
    if (m_saveButton) {
        m_saveButton->setToolTip(tr("Save"));
    }
    if (m_backgroundButton) {
        m_backgroundButton->setToolTip(tr("Background"));
    }
    if (m_resetButton) {
        m_resetButton->setToolTip(tr("Reset Rows"));
    }
    if (m_focusButton) {
        m_focusButton->setToolTip(m_tierFocusMode ? tr("Exit Tier Focus") : tr("Enter Tier Focus"));
    }
}

void AppTitleBar::setDocumentTitle(const QString& title) {
    if (!m_titleEdit) {
        return;
    }
    const QString cleanTitle = title.endsWith(QStringLiteral(" *")) ? title.left(title.size() - 2) : title;
    if (!m_titleEdit->hasFocus() && m_titleEdit->text() != cleanTitle) {
        m_titleEdit->setText(cleanTitle.isEmpty() ? tr("Untitled Tier List") : cleanTitle);
        rememberTitleEditBaseline();
    }
    updateTitleWidth();
}

void AppTitleBar::setTitleEditable(bool editable) {
    m_titleEditable = editable;
    if (!m_titleEdit) {
        return;
    }
    m_titleEdit->setReadOnly(!editable);
    m_titleEdit->setFocusPolicy(editable ? Qt::ClickFocus : Qt::NoFocus);
    if (!editable) {
        m_titleEdit->clearFocus();
        m_titleEdit->deselect();
    }
    m_titleEdit->setStyleSheet(QStringLiteral(
        "QLineEdit#ProjectTitleEdit{background:transparent;border:none;"
        "border-radius:0px;padding:4px 10px;color:palette(window-text);selection-background-color:#7aa2f7;"
        "selection-color:white;}"));
    updateTitleGeometry();
}

void AppTitleBar::setEditorActionsVisible(bool visible) {
    m_editorActionsVisible = visible;
    if (m_buttonGroup) {
        m_buttonGroup->setVisible(visible && !m_tierFocusMode);
    }
}

void AppTitleBar::setSaveActionEnabled(bool enabled) {
    if (m_saveButton) {
        m_saveButton->setEnabled(enabled);
    }
}

void AppTitleBar::setResetRowsActionEnabled(bool enabled) {
    if (m_resetButton) {
        m_resetButton->setEnabled(enabled);
    }
}

void AppTitleBar::setTierFocusMode(bool enabled) {
    if (m_tierFocusMode == enabled) {
        return;
    }
    m_tierFocusMode = enabled;
    if (m_titleEdit) {
        m_titleEdit->setVisible(!enabled);
    }
    if (m_buttonGroup) {
        m_buttonGroup->setVisible(!enabled && m_editorActionsVisible);
    }
    if (m_focusButton) {
        m_focusButton->setToolTip(enabled ? tr("Exit Tier Focus") : tr("Enter Tier Focus"));
        m_focusButton->setIconSize(enabled ? QSize(15, 15) : QSize(19, 19));
    }
    setBackgroundOpacity(0.0);
    setBottomSeparatorVisible(false);
    setToolbarReveal(enabled ? 0.0 : 1.0);
}

void AppTitleBar::submitTitleEdit(bool clearFocus) {
    if (m_submittingTitle || !m_titleEdit || !m_titleEditable) {
        return;
    }
    m_submittingTitle = true;
    const QString title = m_titleEdit->text();
    if (clearFocus && m_titleEdit->hasFocus()) {
        m_titleEdit->clearFocus();
    }
    emit projectTitleEdited(title);
    rememberTitleEditBaseline();
    updateTitleWidth();
    m_submittingTitle = false;
}

void AppTitleBar::cancelTitleEdit() {
    if (!m_titleEdit || !m_titleEditable) {
        return;
    }
    m_cancelingTitleEdit = true;
    m_titleEdit->setText(m_titleEditBaseline);
    if (m_titleEdit->hasFocus()) {
        m_titleEdit->clearFocus();
    }
    updateTitleWidth();
    m_cancelingTitleEdit = false;
}

void AppTitleBar::rememberTitleEditBaseline() {
    if (m_titleEdit) {
        m_titleEditBaseline = m_titleEdit->text();
    }
}

QRect AppTitleBar::globalRectFor(QWidget* widget) const {
    if (!widget) {
        return {};
    }
    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
}

void AppTitleBar::updateTitleWidth() {
    if (!m_titleEdit) {
        return;
    }
    const int textWidth = QFontMetrics(m_titleEdit->font()).horizontalAdvance(m_titleEdit->text());
    const int reservedRight = (m_buttonGroup && m_buttonGroup->isVisible()) ? m_buttonGroup->width() + 28 : 18;
    const int available = qMax(190, width() - reservedRight * 2);
    m_titleEdit->setFixedWidth(qBound(190, textWidth + 50, qMin(520, available)));
    updateTitleGeometry();
}

QSize AppTitleBar::focusRevealSizeHint() const {
    const int toolbarWidth = m_buttonGroup ? m_buttonGroup->sizeHint().width() : 260;
    return QSize(toolbarWidth + 36, 54);
}

bool AppTitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress && watched != m_titleEdit && m_titleEdit &&
        m_titleEdit->hasFocus()) {
        submitTitleEdit(true);
    }
    if (watched == m_titleEdit) {
        if (event->type() == QEvent::FocusIn) {
            rememberTitleEditBaseline();
        } else if (event->type() == QEvent::FocusOut && !m_submittingTitle && !m_cancelingTitleEdit) {
            submitTitleEdit(false);
        } else if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Escape) {
                // Keep title editing commands local to the line edit before page-level shortcuts see them.
                keyEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                submitTitleEdit(true);
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                cancelTitleEdit();
                return true;
            }
        }
    }
    if (watched == m_buttonGroup && m_tierFocusMode) {
        if (event->type() == QEvent::Enter) {
            setToolbarReveal(1.0);
        } else if (event->type() == QEvent::Leave) {
            setToolbarReveal(0.0);
        }
    }
    return vkframeless::WindowTitleBar::eventFilter(watched, event);
}

void AppTitleBar::mousePressEvent(QMouseEvent* event) {
    if (m_titleEdit && m_titleEdit->hasFocus() &&
        !m_titleEdit->geometry().contains(event->position().toPoint())) {
        submitTitleEdit(true);
    }
    vkframeless::WindowTitleBar::mousePressEvent(event);
}

void AppTitleBar::resizeEvent(QResizeEvent* event) {
    vkframeless::WindowTitleBar::resizeEvent(event);
    updateTitleWidth();
}

void AppTitleBar::changeEvent(QEvent* event) {
    vkframeless::WindowTitleBar::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        setBackgroundColor(palette().color(QPalette::Base));
        update();
    }
}

void AppTitleBar::updateTitleGeometry() {
    if (!m_titleEdit) {
        return;
    }
    const QSize titleSize = m_titleEdit->sizeHint();
    const int titleWidth = m_titleEdit->width() > 0 ? m_titleEdit->width() : titleSize.width();
    const int titleHeight = titleSize.height();
    const int x = qMax(0, (width() - titleWidth) / 2);
    const int y = qMax(0, (height() - titleHeight) / 2);
    m_titleEdit->setGeometry(x, y, titleWidth, titleHeight);
    m_titleEdit->raise();
    if (m_buttonGroup) {
        m_buttonGroup->raise();
    }
}

void AppTitleBar::setToolbarReveal(qreal targetOpacity) {
    if (!m_buttonGroupOpacity) {
        return;
    }
    targetOpacity = qBound<qreal>(0.0, targetOpacity, 1.0);
    if (m_revealAnimation && qAbs(m_revealAnimation->endValue().toReal() - targetOpacity) < 0.01) {
        return;
    }
    if (m_revealAnimation) {
        m_revealAnimation->stop();
        m_revealAnimation->deleteLater();
        m_revealAnimation = nullptr;
    }

    auto* animation = new QVariantAnimation(this);
    m_revealAnimation = animation;
    animation->setDuration(160);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(m_buttonGroupOpacity->opacity());
    animation->setEndValue(targetOpacity);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_buttonGroupOpacity->setOpacity(value.toReal());
    });
    connect(animation, &QVariantAnimation::finished, this, [this, animation, targetOpacity]() {
        if (m_revealAnimation == animation) {
            m_revealAnimation = nullptr;
        }
        m_buttonGroupOpacity->setOpacity(targetOpacity);
        animation->deleteLater();
    });
    animation->start();
}

} // namespace tlm
