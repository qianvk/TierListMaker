#include "window/AppDialog.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <QWKCore/windowagentbase.h>
#include <QWKWidgets/widgetwindowagent.h>
#include <vkui/core/VkIcon.h>

namespace tlm {

namespace {
constexpr int kTitleBarHeight = 44;
constexpr int kMacCloseButtonReserve = 24;
constexpr QPoint kMacCloseButtonPosition{18, 15};
constexpr int kSystemButtonReserve = 56;
} // namespace

AppDialog::AppDialog(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowCloseButtonHint),
      m_windowAgent(new QWK::WidgetWindowAgent(this)) {
    setObjectName(QStringLiteral("AppDialog"));
    setWindowTitle(title);
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_DontCreateNativeAncestors);

    buildUi(title);
    installWindowChrome();
}

bool AppDialog::isResizable() const {
    return m_resizable;
}

void AppDialog::setResizable(bool resizable) {
    if (m_resizable == resizable) {
        return;
    }
    m_resizable = resizable;
    if (m_windowAgent) {
        m_windowAgent->setResizable(resizable);
    }
}

void AppDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowTitleChange || event->type() == QEvent::LanguageChange) {
        refreshTitle();
    }
    QDialog::changeEvent(event);
}

void AppDialog::buildUi(const QString& title) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_surface = new QFrame(this);
    m_surface->setObjectName(QStringLiteral("AppDialogSurface"));
    // Native window chrome owns the outer edge and corner clipping. Drawing another rounded
    // frame here produces a visible double border, especially on macOS.
    m_surface->setFrameShape(QFrame::NoFrame);
    m_surface->setStyleSheet(
        QStringLiteral("QFrame#AppDialogSurface{background:palette(window);border:none;}"));
    root->addWidget(m_surface, 1);

    auto* surfaceLayout = new QVBoxLayout(m_surface);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    m_titleBar = new QWidget(m_surface);
    m_titleBar->setObjectName(QStringLiteral("AppDialogTitleBar"));
    m_titleBar->setFixedHeight(kTitleBarHeight);
    auto* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(18, 0, 10, 0);
    titleLayout->setSpacing(8);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    m_nativeButtonArea = new QWidget(m_titleBar);
    m_nativeButtonArea->setFixedWidth(kMacCloseButtonReserve);
    titleLayout->addWidget(m_nativeButtonArea);
#endif
    m_titleLabel = new QLabel(title, m_titleBar);
    m_titleLabel->setObjectName(QStringLiteral("AppDialogTitleLabel"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    titleLayout->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_fallbackCloseButton = new QToolButton(m_titleBar);
    m_fallbackCloseButton->setAutoRaise(true);
    m_fallbackCloseButton->setFocusPolicy(Qt::NoFocus);
    m_fallbackCloseButton->setIcon(vkui::icon(vkui::VkSymbol::Close));
    m_fallbackCloseButton->setIconSize(QSize(16, 16));
    m_fallbackCloseButton->setToolTip(tr("Close"));
    m_fallbackCloseButton->setFixedSize(36, 32);
    connect(m_fallbackCloseButton, &QToolButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(m_fallbackCloseButton, 0, Qt::AlignVCenter);

    m_nativeButtonSpacer = new QWidget(m_titleBar);
    m_nativeButtonSpacer->setFixedWidth(kSystemButtonReserve);
    titleLayout->addWidget(m_nativeButtonSpacer);
    surfaceLayout->addWidget(m_titleBar);

    m_content = new QWidget(m_surface);
    m_content->setObjectName(QStringLiteral("AppDialogContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(18, 0, 18, 18);
    m_contentLayout->setSpacing(14);
    surfaceLayout->addWidget(m_content, 1);
}

void AppDialog::installWindowChrome() {
    const bool setup = m_windowAgent->setup(this);
    bool platformCloseAvailable = false;
    if (setup) {
        m_windowAgent->setResizable(m_resizable);
        platformCloseAvailable = m_windowAgent->installSystemButtons();
        m_windowAgent->addTitleBar(m_titleBar);
        m_windowAgent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysVisible);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        // A close-only dialog keeps the native AppKit control at its normal top-left position;
        // it must not be centered inside a three-button traffic-light reservation.
        m_windowAgent->setSystemButtonPosition(QWK::WindowAgentBase::Close,
                                                kMacCloseButtonPosition);
#elif defined(Q_OS_WIN)
        if (auto* closeButton = m_windowAgent->systemButton(QWK::WindowAgentBase::Close)) {
            closeButton->setProperty("_qwk_top_right_corner_radius", 12.0);
        }
#endif
        if (m_fallbackCloseButton) {
            m_windowAgent->setHitTestVisible(m_fallbackCloseButton, true);
        }
    }

    if (m_fallbackCloseButton) {
        m_fallbackCloseButton->setVisible(!platformCloseAvailable);
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (m_nativeButtonArea) {
        m_nativeButtonArea->setVisible(platformCloseAvailable);
    }
#endif
    if (m_nativeButtonSpacer) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        m_nativeButtonSpacer->setVisible(false);
#else
        m_nativeButtonSpacer->setVisible(platformCloseAvailable);
#endif
    }
}

void AppDialog::refreshTitle() {
    if (m_titleLabel) {
        m_titleLabel->setText(windowTitle());
    }
}

} // namespace tlm
