#include "window/AppTitleBar.h"
#include "window/MainWindow.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QWidget>

#include <algorithm>

#include <QWKWidgets/widgetwindowagent.h>
#include <vkui/core/VkIcon.h>

namespace tlm {

namespace {
constexpr int kTitleBarHorizontalMargin = 18;
constexpr int kTitleBarSpacing = 6;

QToolButton* makeButton(const QString& text, vkui::VkSymbol symbol, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setToolTip(text);
    button->setIcon(vkui::icon(symbol));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setCursor(Qt::ArrowCursor);
    button->setAttribute(Qt::WA_NoMousePropagation, true);
    button->setAutoRaise(true);
    button->setFixedSize(32, 32);
    button->setIconSize(QSize(19, 19));
    return button;
}

[[maybe_unused]] int nativeSystemButtonWidth(const QWidget* widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    Q_UNUSED(widget);
    return 0;
#else
    const auto* mainWindow = widget ? qobject_cast<const MainWindow*>(widget->window()) : nullptr;
    const auto* agent = mainWindow ? mainWindow->windowAgent() : nullptr;
    return agent ? agent->systemButtonAreaGeometry().width() : 0;
#endif
}

[[maybe_unused]] int systemButtonReservedWidth(const QWidget* widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    Q_UNUSED(widget);
    return 18;
#else
    return nativeSystemButtonWidth(widget) + 18;
#endif
}

} // namespace

AppTitleBar::AppTitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(54);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_NoMousePropagation, true);

    // Interactive chrome is parented beside the transparent drag region. This preserves normal
    // Qt style painting for every independent control without propagating hover updates through
    // the transparent title-bar widget and the dynamic page beneath it.
    QWidget* controlsParent = parent ? parent : this;
    m_titleEdit = new QLineEdit(controlsParent);
    m_titleEdit->setObjectName(QStringLiteral("ProjectTitleEdit"));
    m_titleEdit->setAlignment(Qt::AlignCenter);
    m_titleEdit->setPlaceholderText(tr("Untitled Tier List"));
    m_titleEdit->setFocusPolicy(Qt::ClickFocus);
    QFont titleFont = m_titleEdit->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 5);
    m_titleEdit->setFont(titleFont);
    m_titleEdit->setMinimumWidth(48);
    m_titleEdit->setMaximumWidth(520);
    m_titleEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_titleEdit->installEventFilter(this);
    m_titleEdit->setStyleSheet(
        QStringLiteral("QLineEdit#ProjectTitleEdit{background:transparent;border:none;"
                       "border-radius:0px;padding:4px "
                       "10px;}"));
    m_unsavedIndicator = new QLabel(controlsParent);
    m_unsavedIndicator->setObjectName(QStringLiteral("UnsavedIndicator"));
    m_unsavedIndicator->setFixedSize(16, 16);
    m_unsavedIndicator->setPixmap(
        vkui::icon(vkui::VkSymbol::UnsavedIndicator, vkui::VkIconRole::Accent).pixmap(14, 14));
    m_unsavedIndicator->setToolTip(tr("Unsaved changes"));
    m_unsavedIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_unsavedIndicator->hide();
    m_templatesButton = makeButton(tr("Templates"), vkui::VkSymbol::Templates, controlsParent);
    m_backgroundButton =
        makeButton(tr("Background"), vkui::VkSymbol::CanvasBackground, controlsParent);
    m_galleryButton = makeButton(tr("Gallery"), vkui::VkSymbol::PhotoLibrary, controlsParent);
    m_resetButton = makeButton(tr("Reset Rows"), vkui::VkSymbol::Reset, controlsParent);
    m_focusButton = makeButton(tr("Enter Tier Focus"), vkui::VkSymbol::FocusTarget, controlsParent);
    connect(m_templatesButton, &QToolButton::clicked, this,
            [this]() { emit templatesRequested(m_templatesButton); });
    connect(m_backgroundButton, &QToolButton::clicked, this,
            [this]() { emit backgroundRequested(m_backgroundButton); });
    connect(m_galleryButton, &QToolButton::clicked, this,
            [this]() { emit galleryRequested(m_galleryButton); });
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

AppTitleBar::~AppTitleBar() {
    removeTitleEditOutsideClickFilter();
    // Overlay controls are visual siblings so that their dirty regions never traverse the
    // transparent drag region. AppTitleBar remains their logical owner.
    delete m_titleEdit;
    delete m_unsavedIndicator;
    delete m_templatesButton;
    delete m_backgroundButton;
    delete m_galleryButton;
    delete m_resetButton;
    delete m_focusButton;
}

void AppTitleBar::retranslateUi() {
    if (m_titleEdit) {
        m_titleEdit->setPlaceholderText(tr("Untitled Tier List"));
    }
    if (m_templatesButton) {
        m_templatesButton->setToolTip(tr("Templates"));
    }
    if (m_backgroundButton) {
        m_backgroundButton->setToolTip(tr("Background"));
    }
    if (m_galleryButton) {
        m_galleryButton->setToolTip(tr("Gallery"));
    }
    if (m_resetButton) {
        m_resetButton->setToolTip(tr("Reset Rows"));
    }
    if (m_focusButton) {
        m_focusButton->setToolTip(m_tierFocusMode ? tr("Exit Tier Focus") : tr("Enter Tier Focus"));
    }
    if (m_unsavedIndicator) {
        m_unsavedIndicator->setToolTip(tr("Unsaved changes"));
        m_unsavedIndicator->setPixmap(
            vkui::icon(vkui::VkSymbol::UnsavedIndicator, vkui::VkIconRole::Accent).pixmap(14, 14));
    }
}

void AppTitleBar::setDocumentTitle(const QString& title) {
    if (!m_titleEdit) {
        return;
    }
    const QString cleanTitle =
        title.endsWith(QStringLiteral(" *")) ? title.left(title.size() - 2) : title;
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
    m_titleEdit->setAttribute(Qt::WA_TransparentForMouseEvents, !editable);
    m_titleEdit->setCursor(editable ? Qt::IBeamCursor : Qt::ArrowCursor);
    if (!editable) {
        m_titleEdit->clearFocus();
        m_titleEdit->deselect();
    }
    m_titleEdit->setStyleSheet(
        QStringLiteral("QLineEdit#ProjectTitleEdit{background:transparent;border:none;"
                       "border-radius:0px;padding:4px "
                       "10px;}"));
    updateTitleGeometry();
}

void AppTitleBar::setEditorActionsVisible(bool visible) {
    m_editorActionsVisible = visible;
    setActionButtonsVisible(visible && !m_tierFocusMode);
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
    setActionButtonsVisible(!enabled && m_editorActionsVisible);
    if (m_focusButton) {
        m_focusButton->setToolTip(enabled ? tr("Exit Tier Focus") : tr("Enter Tier Focus"));
        m_focusButton->setIconSize(enabled ? QSize(15, 15) : QSize(19, 19));
    }
}

void AppTitleBar::setUnsavedIndicatorVisible(bool visible) {
    if (!m_unsavedIndicator || m_unsavedIndicator->isVisible() == visible) {
        return;
    }
    m_unsavedIndicator->setVisible(visible);
    updateTitleGeometry();
}

void AppTitleBar::setLeadingReservedWidth(int width) {
    const int next = qMax(0, width);
    if (m_leadingReservedWidth == next) {
        return;
    }
    m_leadingReservedWidth = next;
    updateTitleWidth();
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

void AppTitleBar::updateTitleWidth() {
    if (!m_titleEdit) {
        return;
    }
    const QString measuredText =
        m_titleEdit->text().isEmpty() ? m_titleEdit->placeholderText() : m_titleEdit->text();
    const int textWidth = QFontMetrics(m_titleEdit->font()).horizontalAdvance(measuredText);
    const int actionWidth = actionButtonsWidth() + (actionButtonsWidth() > 0 ? 28 : 18);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const int reservedLeft = 18;
    const int reservedRight = actionWidth;
#else
    const int reservedLeft = m_leadingReservedWidth + actionWidth;
    const int reservedRight = systemButtonReservedWidth(this);
#endif
    constexpr int kMinimumReadableTitleWidth = 56;
    constexpr int kTitleHorizontalPadding = 34;
    const int reserved = qMax(reservedLeft, reservedRight);
    const int available = qMax(kMinimumReadableTitleWidth, width() - reserved * 2);
    m_titleEdit->setFixedWidth(qBound(kMinimumReadableTitleWidth,
                                      textWidth + kTitleHorizontalPadding, qMin(520, available)));
    updateTitleGeometry();
}

QSize AppTitleBar::focusRevealSizeHint() const {
    return QSize(qMax(260, actionButtonsWidth()) + 36, 54);
}

QLineEdit* AppTitleBar::titleEditor() const {
    return m_titleEdit;
}

QList<QWidget*> AppTitleBar::interactiveWidgets() const {
    return {m_titleEdit, m_templatesButton, m_backgroundButton, m_galleryButton, m_resetButton,
            m_focusButton};
}

void AppTitleBar::raiseChrome() {
    raise();
    if (m_titleEdit) {
        m_titleEdit->raise();
    }
    if (m_unsavedIndicator) {
        m_unsavedIndicator->raise();
    }
    for (QToolButton* button :
         {m_templatesButton, m_backgroundButton, m_galleryButton, m_resetButton, m_focusButton}) {
        if (button) {
            button->raise();
        }
    }
}

bool AppTitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (m_titleEditOutsideClickFilterInstalled && m_titleEdit && m_titleEdit->hasFocus() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::NonClientAreaMouseButtonPress)) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect titleRect(m_titleEdit->mapToGlobal(QPoint(0, 0)), m_titleEdit->size());
        if (!titleRect.contains(mouseEvent->globalPosition().toPoint())) {
            submitTitleEdit(true);
        }
    }
    if (event->type() == QEvent::MouseButtonPress && watched != m_titleEdit && m_titleEdit &&
        m_titleEdit->hasFocus()) {
        submitTitleEdit(true);
    }
    if (watched == m_titleEdit) {
        if (event->type() == QEvent::FocusIn) {
            rememberTitleEditBaseline();
            installTitleEditOutsideClickFilter();
        } else if (event->type() == QEvent::FocusOut && !m_submittingTitle &&
                   !m_cancelingTitleEdit) {
            submitTitleEdit(false);
            removeTitleEditOutsideClickFilter();
        } else if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Escape) {
                // Keep title editing commands local to the line edit before page-level shortcuts
                // see them.
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
    return QWidget::eventFilter(watched, event);
}

void AppTitleBar::mousePressEvent(QMouseEvent* event) {
    const QRect titleRect = m_titleEdit
                                ? QRect(mapFromParent(m_titleEdit->pos()), m_titleEdit->size())
                                : QRect{};
    if (m_titleEdit && m_titleEdit->hasFocus() &&
        !titleRect.contains(event->position().toPoint())) {
        submitTitleEdit(true);
    }
    QWidget::mousePressEvent(event);
}

void AppTitleBar::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    updateTitleGeometry();
}

void AppTitleBar::paintEvent(QPaintEvent* event) {
    // The registered QWindowKit drag region is intentionally paint-free. Dynamic page content is
    // visible below it, while sibling controls keep independent interaction and dirty regions.
    event->accept();
}

void AppTitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTitleWidth();
}

void AppTitleBar::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
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
    const int x = pos().x() + qMax(0, (width() - titleWidth) / 2);
    const int y = pos().y() + qMax(0, (height() - titleHeight) / 2);
    m_titleEdit->setGeometry(x, y, titleWidth, titleHeight);
    m_titleEdit->raise();
    if (m_unsavedIndicator) {
        const int indicatorX = qMin(pos().x() + width() - m_unsavedIndicator->width() - 4,
                                    m_titleEdit->geometry().right() + 4);
        const int indicatorY =
            pos().y() + qMax(0, (height() - m_unsavedIndicator->height()) / 2);
        m_unsavedIndicator->move(qMax(pos().x(), indicatorX), indicatorY);
        m_unsavedIndicator->raise();
    }

    const int totalActionWidth = actionButtonsWidth();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    int actionX = pos().x() + width() - kTitleBarHorizontalMargin - totalActionWidth;
#else
    int actionX = pos().x() + kTitleBarHorizontalMargin + m_leadingReservedWidth;
#endif
    for (QToolButton* button :
         {m_templatesButton, m_backgroundButton, m_galleryButton, m_resetButton, m_focusButton}) {
        if (!button || !button->isVisible()) {
            continue;
        }
        button->move(actionX, pos().y() + qMax(0, (height() - button->height()) / 2));
        button->raise();
        actionX += button->width() + kTitleBarSpacing;
    }
}

void AppTitleBar::setActionButtonsVisible(bool visible) {
    for (QToolButton* button :
         {m_templatesButton, m_backgroundButton, m_galleryButton, m_resetButton, m_focusButton}) {
        if (button) {
            button->setVisible(visible);
        }
    }
    updateTitleWidth();
}

int AppTitleBar::actionButtonsWidth() const {
    int width = 0;
    int visibleButtons = 0;
    for (const QToolButton* button :
         {m_templatesButton, m_backgroundButton, m_galleryButton, m_resetButton, m_focusButton}) {
        if (!button || !button->isVisible()) {
            continue;
        }
        width += button->width();
        ++visibleButtons;
    }
    return width + qMax(0, visibleButtons - 1) * kTitleBarSpacing;
}

void AppTitleBar::installTitleEditOutsideClickFilter() {
    if (m_titleEditOutsideClickFilterInstalled || !qApp) {
        return;
    }
    qApp->installEventFilter(this);
    m_titleEditOutsideClickFilterInstalled = true;
}

void AppTitleBar::removeTitleEditOutsideClickFilter() {
    if (!m_titleEditOutsideClickFilterInstalled || !qApp) {
        return;
    }
    qApp->removeEventFilter(this);
    m_titleEditOutsideClickFilterInstalled = false;
}

} // namespace tlm
