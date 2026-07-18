#pragma once

#include "update/AppUpdater.h"

#include <QToolButton>
#include <QVariantAnimation>

namespace tlm {

/** Compact update action with event-driven attention and download progress states. */
class UpdateButton final : public QToolButton {
public:
    explicit UpdateButton(QWidget* parent = nullptr);

    void setUpdateState(UpdateState state);
    void setDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void setReducedMotion(bool reduced);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshIcon();
    void playAttentionAnimation();

    QVariantAnimation m_attentionAnimation;
    UpdateState m_state{UpdateState::Idle};
    qint64 m_bytesReceived{0};
    qint64 m_bytesTotal{-1};
    qreal m_attention{0.0};
    bool m_reducedMotion{false};
};

} // namespace tlm
