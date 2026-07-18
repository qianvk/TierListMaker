#include "update/UpdateButton.h"

#include "theme/Theme.h"

#include <QPaintEvent>
#include <QPainter>

#include <vkui/core/VkIcon.h>

#include <cmath>
#include <numbers>

namespace tlm {

UpdateButton::UpdateButton(QWidget* parent) : QToolButton(parent), m_attentionAnimation(this) {
    setObjectName(QStringLiteral("UpdateButton"));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setIconSize(QSize(18, 18));
    setAutoRaise(true);
    hide();

    m_attentionAnimation.setDuration(720);
    m_attentionAnimation.setStartValue(0.0);
    m_attentionAnimation.setEndValue(1.0);
    m_attentionAnimation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_attentionAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_attention = value.toReal();
                update();
            });
    connect(&m_attentionAnimation, &QVariantAnimation::finished, this, [this]() {
        m_attention = 0.0;
        update();
    });
    refreshIcon();
}

void UpdateButton::setUpdateState(UpdateState state) {
    if (m_state == state) {
        return;
    }
    const UpdateState previous = m_state;
    m_state = state;
    const bool actionable = state == UpdateState::Available || state == UpdateState::Ready;
    const bool visible = actionable || state == UpdateState::Downloading ||
                         state == UpdateState::Installing;
    setVisible(visible);
    setEnabled(actionable);
    refreshIcon();
    if (state == UpdateState::Available && previous != UpdateState::Available) {
        playAttentionAnimation();
    }
    update();
}

void UpdateButton::setDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (m_bytesReceived == bytesReceived && m_bytesTotal == bytesTotal) {
        return;
    }
    m_bytesReceived = qMax<qint64>(0, bytesReceived);
    m_bytesTotal = bytesTotal;
    if (m_state == UpdateState::Downloading) {
        update();
    }
}

void UpdateButton::setReducedMotion(bool reduced) {
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (reduced) {
        m_attentionAnimation.stop();
        m_attention = 0.0;
        update();
    }
}

void UpdateButton::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);
    if (m_state != UpdateState::Downloading && m_state != UpdateState::Ready &&
        m_attention <= 0.0) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor accent = activeThemeTokens().accent;
    if (m_attention > 0.0) {
        accent.setAlphaF(0.18 + 0.48 * std::sin(m_attention * std::numbers::pi));
    }
    QPen pen(accent, 2.0, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    const QRectF progressRect = QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0);

    if (m_state == UpdateState::Downloading) {
        const qreal progress = m_bytesTotal > 0
                                   ? qBound(0.0, static_cast<qreal>(m_bytesReceived) /
                                                     static_cast<qreal>(m_bytesTotal),
                                            1.0)
                                   : 0.25;
        painter.drawArc(progressRect, 90 * 16, -qRound(progress * 360.0 * 16.0));
    } else {
        painter.drawEllipse(progressRect);
    }
}

void UpdateButton::refreshIcon() {
    const bool ready = m_state == UpdateState::Ready || m_state == UpdateState::Installing;
    setIcon(vkui::icon(ready ? vkui::VkSymbol::Checkmark : vkui::VkSymbol::Download,
                       vkui::VkIconRole::Accent));
}

void UpdateButton::playAttentionAnimation() {
    if (m_reducedMotion) {
        return;
    }
    m_attentionAnimation.stop();
    m_attentionAnimation.start();
}

} // namespace tlm
