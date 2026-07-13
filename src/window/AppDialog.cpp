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
constexpr int kSystemButtonReserve = 56;
} // namespace

AppDialog::AppDialog(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowCloseButtonHint),
      m_windowAgent(new QWK::WidgetWindowAgent(this)) {
    setObjectName(QStringLiteral("AppDialog"));
    setWindowTitle(title);
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors);

    buildUi(title);
    installWindowChrome();
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
    m_surface->setStyleSheet(QStringLiteral(
        "QFrame#AppDialogSurface{background:palette(window);border:1px solid palette(mid);"
        "border-radius:12px;}"));
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
    titleLayout->addSpacing(kSystemButtonReserve);
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
        platformCloseAvailable = m_windowAgent->installSystemButtons();
        m_windowAgent->addTitleBar(m_titleBar);
        m_windowAgent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysVisible);
        if (m_fallbackCloseButton) {
            m_windowAgent->setHitTestVisible(m_fallbackCloseButton, true);
        }
    }

    if (m_fallbackCloseButton) {
        m_fallbackCloseButton->setVisible(!platformCloseAvailable);
    }
    if (m_nativeButtonSpacer) {
        m_nativeButtonSpacer->setVisible(platformCloseAvailable);
    }
}

void AppDialog::refreshTitle() {
    if (m_titleLabel) {
        m_titleLabel->setText(windowTitle());
    }
}

} // namespace tlm
