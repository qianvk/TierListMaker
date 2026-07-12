#include "window/AppTitleBar.h"

#include <vkframeless/FramelessWindow.h>

#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace tlm {

namespace {
constexpr int kTitleBarHorizontalMargin = 18;
constexpr int kTitleBarSpacing = 6;

#if defined(Q_OS_WIN)
constexpr int kWindows11CaptionIconSize = 10;
constexpr QChar kWindowsChromeMinimize(0xe921);
constexpr QChar kWindowsChromeMaximize(0xe922);
constexpr QChar kWindowsChromeRestore(0xe923);
constexpr QChar kWindowsChromeClose(0xe8bb);
#endif

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

int nativeSystemButtonWidth(const QWidget* widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    Q_UNUSED(widget);
    return 0;
#else
    const auto* frameless = widget ? qobject_cast<const vkframeless::FramelessWindow*>(widget->window()) : nullptr;
    return frameless ? frameless->systemButtonReservedWidth() : 0;
#endif
}

int systemButtonReservedWidth(const QWidget* widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    Q_UNUSED(widget);
    return 18;
#else
    return nativeSystemButtonWidth(widget) + 18;
#endif
}

#if defined(Q_OS_WIN)
UINT dpiForWidget(const QWidget* widget) {
    if (widget && widget->window()) {
        HWND hwnd = reinterpret_cast<HWND>(widget->window()->winId());
        if (hwnd)
            return ::GetDpiForWindow(hwnd);
    }
    return USER_DEFAULT_SCREEN_DPI;
}

int logicalSystemMetric(const QWidget* widget, int metric) {
    const UINT dpi = dpiForWidget(widget);
    return ::MulDiv(::GetSystemMetricsForDpi(metric, dpi), USER_DEFAULT_SCREEN_DPI, dpi);
}

int logicalPixelsFromPhysical(const QWidget* widget, int value) {
    return ::MulDiv(value, USER_DEFAULT_SCREEN_DPI, dpiForWidget(widget));
}

QRect nativeCaptionButtonBoundsPhysical(const QWidget* widget) {
    if (!widget || !widget->window()) {
        return {};
    }

    HWND hwnd = reinterpret_cast<HWND>(widget->window()->winId());
    RECT windowRect = {0, 0, 0, 0};
    if (!hwnd || !::GetWindowRect(hwnd, &windowRect)) {
        return {};
    }

    RECT bounds = {0, 0, 0, 0};
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd,
                                          DWMWA_CAPTION_BUTTON_BOUNDS,
                                          &bounds,
                                          sizeof(bounds))) &&
        bounds.right > bounds.left && bounds.bottom > bounds.top) {
        const int windowWidth = static_cast<int>(windowRect.right - windowRect.left);
        const int windowHeight = static_cast<int>(windowRect.bottom - windowRect.top);
        const bool windowRelative = bounds.left >= 0 && bounds.top >= 0 &&
                                    bounds.right <= windowWidth && bounds.bottom <= windowHeight;
        if (!windowRelative) {
            ::OffsetRect(&bounds, -windowRect.left, -windowRect.top);
        }
        return QRect(QPoint(bounds.left, bounds.top),
                     QSize(bounds.right - bounds.left, bounds.bottom - bounds.top));
    }

    const UINT dpi = dpiForWidget(widget);
    const int width = ::GetSystemMetricsForDpi(SM_CXSIZE, dpi) * 3;
    const int height = ::GetSystemMetricsForDpi(SM_CYSIZE, dpi);
    const int windowWidth = static_cast<int>(windowRect.right - windowRect.left);
    return QRect(std::max(0, windowWidth - width), 0, width, height);
}

QRect widgetLogicalCaptionButtonRect(const QWidget* widget) {
    const QRect nativeBounds = nativeCaptionButtonBoundsPhysical(widget);
    if (!widget || !widget->window() || !nativeBounds.isValid()) {
        return {};
    }

    const QPoint widgetWindowPos = widget->mapTo(widget->window(), QPoint(0, 0));
    return QRect(logicalPixelsFromPhysical(widget, nativeBounds.x()) - widgetWindowPos.x(),
                 logicalPixelsFromPhysical(widget, nativeBounds.y()) - widgetWindowPos.y(),
                 logicalPixelsFromPhysical(widget, nativeBounds.width()),
                 logicalPixelsFromPhysical(widget, nativeBounds.height()));
}

QFont windows11CaptionIconFont() {
    QFont font;
    font.setFamilies({QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
    font.setPixelSize(kWindows11CaptionIconSize);
    font.setStyleStrategy(QFont::PreferNoShaping);
    return font;
}

QChar windows11CaptionGlyph(int index, bool maximized) {
    switch (index) {
    case 0:
        return kWindowsChromeMinimize;
    case 1:
        return maximized ? kWindowsChromeRestore : kWindowsChromeMaximize;
    case 2:
        return kWindowsChromeClose;
    default:
        return QChar();
    }
}

bool isDarkWindowPalette(const QWidget* widget) {
    const QColor windowColor = widget ? widget->palette().color(QPalette::Window) : QColor(Qt::white);
    return windowColor.lightness() < 128;
}

QColor windows11CaptionHoverColor(bool closeButton, bool pressed, bool darkPalette) {
    if (closeButton) {
        return pressed ? QColor(153, 27, 18) : QColor(196, 43, 28);
    }

    const int alpha = pressed ? 30 : 18;
    return darkPalette ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha);
}

QColor windows11CaptionGlyphColor(bool closeButton, bool hovered, bool activeWindow, bool darkPalette) {
    if (closeButton && hovered) {
        return QColor(Qt::white);
    }
    if (!activeWindow) {
        return darkPalette ? QColor(150, 150, 150) : QColor(120, 120, 120);
    }
    return darkPalette ? QColor(255, 255, 255) : QColor(32, 32, 32);
}
#endif
} // namespace

AppTitleBar::AppTitleBar(QWidget* parent) : vkframeless::WindowTitleBar(parent) {
    setPreferredHeight(54);
    setBottomSeparatorVisible(false);
    setBackgroundOpacity(0.0);
    setBackgroundColor(palette().color(QPalette::Base));

    auto* layout = contentLayout();
    updateLayoutMargins();
    layout->setSpacing(kTitleBarSpacing);
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item;
    }

    m_titleEdit = new QLineEdit(this);
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
    m_titleEdit->setStyleSheet(QStringLiteral(
        "QLineEdit#ProjectTitleEdit{background:transparent;border:none;"
        "border-radius:0px;padding:4px 10px;color:palette(window-text);selection-background-color:#7aa2f7;"
        "selection-color:white;}"));
    m_newButton = makeButton(tr("New"), QStringLiteral(":/icons/plus.svg"), this);
    m_openButton = makeButton(tr("Open"), QStringLiteral(":/icons/folder.svg"), this);
    m_saveButton = makeButton(tr("Save"), QStringLiteral(":/icons/save.svg"), this);
    m_backgroundButton = makeButton(tr("Background"), QStringLiteral(":/icons/image.svg"), this);
    m_galleryButton = makeButton(tr("Gallery"), QStringLiteral(":/icons/gallery.svg"), this);
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
    for (QToolButton* button : {m_newButton, m_openButton, m_saveButton, m_backgroundButton, m_galleryButton, m_resetButton,
                                m_focusButton}) {
        button->installEventFilter(this);
        buttonsLayout->addWidget(button);
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    layout->addStretch(1);
    layout->addWidget(m_buttonGroup);
#else
    layout->addWidget(m_buttonGroup);
    layout->addStretch(1);
#endif

    connect(m_newButton, &QToolButton::clicked, this, &AppTitleBar::newRequested);
    connect(m_openButton, &QToolButton::clicked, this, &AppTitleBar::openRequested);
    connect(m_saveButton, &QToolButton::clicked, this, &AppTitleBar::saveRequested);
    connect(m_backgroundButton, &QToolButton::clicked, this,
            [this]() { emit backgroundRequested(globalRectFor(m_backgroundButton)); });
    connect(m_galleryButton, &QToolButton::clicked, this,
            [this]() { emit galleryRequested(globalRectFor(m_galleryButton)); });
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
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    removeTitleEditOutsideClickFilter();
#endif
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
    if (m_galleryButton) {
        m_galleryButton->setToolTip(tr("Gallery"));
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

void AppTitleBar::setLeadingReservedWidth(int width) {
    const int next = qMax(0, width);
    if (m_leadingReservedWidth == next) {
        return;
    }
    m_leadingReservedWidth = next;
    updateLayoutMargins();
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
    const QString measuredText = m_titleEdit->text().isEmpty() ? m_titleEdit->placeholderText()
                                                               : m_titleEdit->text();
    const int textWidth = QFontMetrics(m_titleEdit->font()).horizontalAdvance(measuredText);
    const int actionWidth = (m_buttonGroup && m_buttonGroup->isVisible()) ? m_buttonGroup->width() + 28 : 18;
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
    m_titleEdit->setFixedWidth(qBound(kMinimumReadableTitleWidth, textWidth + kTitleHorizontalPadding,
                                      qMin(520, available)));
    updateTitleGeometry();
}

QSize AppTitleBar::focusRevealSizeHint() const {
    const int toolbarWidth = m_buttonGroup ? m_buttonGroup->sizeHint().width() : 260;
    return QSize(toolbarWidth + 36, 54);
}

QRect AppTitleBar::galleryButtonGlobalRect() const {
    return globalRectFor(m_galleryButton);
}

bool AppTitleBar::eventFilter(QObject* watched, QEvent* event) {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    if (m_titleEditOutsideClickFilterInstalled && m_titleEdit && m_titleEdit->hasFocus() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::NonClientAreaMouseButtonPress)) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect titleRect(m_titleEdit->mapToGlobal(QPoint(0, 0)), m_titleEdit->size());
        if (!titleRect.contains(mouseEvent->globalPosition().toPoint())) {
            submitTitleEdit(true);
        }
    }
#endif
    if (event->type() == QEvent::MouseButtonPress && watched != m_titleEdit && m_titleEdit &&
        m_titleEdit->hasFocus()) {
        submitTitleEdit(true);
    }
    if (watched == m_titleEdit) {
        if (event->type() == QEvent::FocusIn) {
            rememberTitleEditBaseline();
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
            installTitleEditOutsideClickFilter();
#endif
        } else if (event->type() == QEvent::FocusOut && !m_submittingTitle && !m_cancelingTitleEdit) {
            submitTitleEdit(false);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
            removeTitleEditOutsideClickFilter();
#endif
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

void AppTitleBar::paintEvent(QPaintEvent* event) {
    vkframeless::WindowTitleBar::paintEvent(event);
#if defined(Q_OS_WIN)
    QPainter painter(this);
    paintWindowsNativeCaptionButtons(&painter);
#endif
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

void AppTitleBar::updateLayoutMargins() {
    auto* layout = contentLayout();
    if (!layout) {
        return;
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    layout->setContentsMargins(kTitleBarHorizontalMargin, 0, kTitleBarHorizontalMargin, 0);
#else
    layout->setContentsMargins(kTitleBarHorizontalMargin + m_leadingReservedWidth, 0,
                               kTitleBarHorizontalMargin, 0);
#endif
}

#if defined(Q_OS_WIN)
QRect AppTitleBar::windowsCaptionButtonVisualRect() const {
    QRect area = widgetLogicalCaptionButtonRect(this);
    if (area.isValid()) {
        return area.intersected(rect());
    }

    const int buttonAreaWidth = nativeSystemButtonWidth(this);
    if (buttonAreaWidth <= 0) {
        return {};
    }

    const int height = qMax(1, logicalSystemMetric(this, SM_CYSIZE));
    return QRect(qMax(0, width() - buttonAreaWidth), 0, buttonAreaWidth,
                 qMin(qMax(1, this->height()), height));
}

void AppTitleBar::paintWindowsNativeCaptionButtons(QPainter* painter) const {
    const QRect area = windowsCaptionButtonVisualRect();
    if (!painter || !area.isValid()) {
        return;
    }

    QWidget* topLevel = window();
    if (!topLevel) {
        return;
    }

    const QPoint cursorPos = mapFromGlobal(QCursor::pos());
    const int buttonWidth = qMax(1, area.width() / 3);
    const bool maximized = topLevel->isMaximized() || topLevel->isFullScreen();
    const bool activeWindow = topLevel->isActiveWindow();
    const bool mousePressed = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool darkPalette = isDarkWindowPalette(this);

    painter->save();
    painter->setClipRect(area);
    painter->setFont(windows11CaptionIconFont());

    for (int index = 0; index < 3; ++index) {
        const int x = area.left() + buttonWidth * index;
        const int width = (index == 2) ? area.right() + 1 - x : buttonWidth;
        const QRect buttonRect(x, area.top(), width, area.height());
        const bool closeButton = index == 2;
        const bool hovered = buttonRect.contains(cursorPos);
        const bool pressed = hovered && mousePressed;

        if (hovered) {
            painter->fillRect(buttonRect, windows11CaptionHoverColor(closeButton, pressed, darkPalette));
        }

        painter->setPen(windows11CaptionGlyphColor(closeButton, hovered, activeWindow, darkPalette));
        painter->drawText(buttonRect, Qt::AlignCenter, QString(windows11CaptionGlyph(index, maximized)));
    }

    painter->restore();
}
#endif

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

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
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
#endif

} // namespace tlm
