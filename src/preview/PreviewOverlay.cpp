#include "preview/PreviewOverlay.h"

#include "logging/Logger.h"
#include "settings/AppSettings.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShortcutEvent>
#include <QWindow>

#include <algorithm>

namespace tlm {

namespace {
constexpr int kProjectionMaximumExtent = 360;
constexpr int kProjectionDownsampleFactor = 5;
constexpr int kImageCornerRadius = 16;
constexpr int kApertureMargin = 18;

QStringView backgroundModeName(PreviewBackgroundMode mode) {
    return mode == PreviewBackgroundMode::SelfImage ? u"self-image" : u"none";
}

bool isBlockingInputEvent(QEvent::Type type) {
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Shortcut:
    case QEvent::ShortcutOverride:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::NativeGesture:
        return true;
    default:
        return false;
    }
}
} // namespace

PreviewOverlay::PreviewOverlay(QWidget* parent)
    : QWidget(parent), m_backgroundMode(PreviewBackgroundMode::None),
      m_animationGroup(new QParallelAnimationGroup(this)),
      m_geometryAnimation(new QPropertyAnimation(this, "previewGeometry", m_animationGroup)),
      m_backdropAnimation(new QPropertyAnimation(this, "backdropProgress", m_animationGroup)) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setToolTip(tr("Click outside to close. Double-click image to close."));
    setProperty("tlmToolTipProvider", QVariant::fromValue(static_cast<QObject*>(this)));

    m_animationGroup->addAnimation(m_geometryAnimation);
    m_animationGroup->addAnimation(m_backdropAnimation);
    connect(m_animationGroup, &QParallelAnimationGroup::finished, this, [this]() {
        if (!m_closing) {
            return;
        }
        m_closing = false;
        m_open = false;
        hide();
        setInputBarrierActive(false);
        m_pixmap = {};
        m_projectionCache = {};
        emit closed();
    });
    hide();
}

PreviewOverlay::~PreviewOverlay() {
    setInputBarrierActive(false);
}

void PreviewOverlay::setPreviewGeometry(const QRect& rect) {
    if (m_previewGeometry == rect) {
        return;
    }
    const QRect dirty = m_previewGeometry.united(rect).adjusted(-28, -28, 28, 28);
    m_previewGeometry = rect;
    update(dirty.intersected(this->rect()));
}

void PreviewOverlay::setBackdropProgress(qreal progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    if (qFuzzyCompare(m_backdropProgress, progress)) {
        return;
    }
    m_backdropProgress = progress;
    update();
}

void PreviewOverlay::setBackgroundMode(PreviewBackgroundMode mode) {
    if (m_backgroundMode == mode) {
        return;
    }
    m_backgroundMode = mode;
    rebuildProjectionCache();
    update();
    Logger::info(QStringLiteral("ui.preview.background mode=%1")
                     .arg(backgroundModeName(mode).toString()));
}

QString PreviewOverlay::toolTipTextAt(QPoint position) const {
    return m_previewGeometry.contains(position) ? tr("Double-click image to close")
                                                : tr("Click to close preview");
}

void PreviewOverlay::openPreview(const QRect& sourceRectInWindow, const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return;
    }

    m_animationGroup->stop();
    m_pixmap = pixmap;
    setGeometry(parentWidget() ? parentWidget()->rect() : geometry());
    const QPoint sourceTopLeft = window() ? mapFrom(window(), sourceRectInWindow.topLeft())
                                          : sourceRectInWindow.topLeft();
    m_sourceGeometry = QRect(sourceTopLeft, sourceRectInWindow.size());
    if (!m_sourceGeometry.isValid()) {
        m_sourceGeometry = QRect(rect().center() - QPoint(20, 20), QSize(40, 40));
    }
    rebuildProjectionCache();

    const bool wasOpen = m_open;
    m_open = true;
    m_closing = false;
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    setInputBarrierActive(true);
    if (!wasOpen) {
        setPreviewGeometry(m_sourceGeometry);
        setBackdropProgress(0.0);
        emit opened();
    }
    const QRect target = targetRectForPixmap(m_pixmap);
    Logger::info(QStringLiteral("ui.preview.open mode=%1 source=(%2,%3,%4,%5) "
                                "target=(%6,%7,%8,%9)")
                     .arg(backgroundModeName(m_backgroundMode).toString())
                     .arg(m_sourceGeometry.x())
                     .arg(m_sourceGeometry.y())
                     .arg(m_sourceGeometry.width())
                     .arg(m_sourceGeometry.height())
                     .arg(target.x())
                     .arg(target.y())
                     .arg(target.width())
                     .arg(target.height()));
    animateTo(m_previewGeometry, target, m_backdropProgress, 1.0, false);
}

void PreviewOverlay::closePreview() {
    if (!m_open || m_closing) {
        return;
    }
    Logger::info(QStringLiteral("ui.preview.close target=(%1,%2,%3,%4)")
                     .arg(m_sourceGeometry.x())
                     .arg(m_sourceGeometry.y())
                     .arg(m_sourceGeometry.width())
                     .arg(m_sourceGeometry.height()));
    m_animationGroup->stop();
    animateTo(m_previewGeometry, m_sourceGeometry, m_backdropProgress, 0.0, true);
}

bool PreviewOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (!m_open || !event || !isBlockingInputEvent(event->type()) ||
        isOverlayDispatchObject(watched)) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Shortcut) {
        auto* shortcutEvent = static_cast<QShortcutEvent*>(event);
        const QKeySequence key = shortcutEvent->key();
        if (key.matches(QKeySequence(Qt::Key_Escape)) == QKeySequence::ExactMatch ||
            key.matches(QKeySequence(Qt::Key_Space)) == QKeySequence::ExactMatch) {
            closePreview();
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Space) {
            closePreview();
        }
    }

    event->accept();
    return true;
}

void PreviewOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    if (m_backgroundMode == PreviewBackgroundMode::SelfImage &&
        !m_projectionCache.isNull()) {
        painter.save();
        painter.setOpacity(m_backdropProgress);
        painter.drawPixmap(rect(), m_projectionCache);
        painter.restore();

        painter.fillRect(rect(), QColor(0, 0, 0, qRound(72.0 * m_backdropProgress)));
        QPainterPath outsideAperture;
        outsideAperture.setFillRule(Qt::OddEvenFill);
        outsideAperture.addRect(rect());
        const QRectF aperture = QRectF(m_previewGeometry).adjusted(
            -kApertureMargin, -kApertureMargin, kApertureMargin, kApertureMargin);
        outsideAperture.addRoundedRect(aperture, kImageCornerRadius + kApertureMargin,
                                      kImageCornerRadius + kApertureMargin);
        painter.fillPath(outsideAperture,
                         QColor(0, 0, 0, qRound(70.0 * m_backdropProgress)));
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, qRound(87.0 * m_backdropProgress)));
    }

    if (m_pixmap.isNull() || m_previewGeometry.isEmpty()) {
        return;
    }

    QColor shadow = activeThemeTokens().shadow;
    for (int spread = 10; spread >= 2; spread -= 2) {
        const QRectF shadowRect = QRectF(m_previewGeometry).adjusted(-spread, -spread, spread, spread);
        shadow.setAlpha(qRound((12.0 - spread) * 2.2 * m_backdropProgress));
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(shadowRect, kImageCornerRadius + spread,
                                  kImageCornerRadius + spread);
        painter.fillPath(shadowPath, shadow);
    }

    const qreal radius = std::min<qreal>(
        kImageCornerRadius,
        std::max<qreal>(3.0, std::min(m_previewGeometry.width(), m_previewGeometry.height()) * 0.18));
    QPainterPath imagePath;
    imagePath.addRoundedRect(m_previewGeometry, radius, radius);
    painter.save();
    painter.setClipPath(imagePath);
    painter.fillRect(m_previewGeometry, activeThemeTokens().elevatedBackground);
    painter.drawPixmap(m_previewGeometry, m_pixmap);
    painter.restore();
}

void PreviewOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !m_previewGeometry.contains(event->pos())) {
        Logger::info(QStringLiteral("ui.preview.close.request source=outside-click pos=(%1,%2)")
                         .arg(event->position().x())
                         .arg(event->position().y()));
        closePreview();
    }
    event->accept();
}

void PreviewOverlay::mouseMoveEvent(QMouseEvent* event) {
    const QString hint = toolTipTextAt(event->pos());
    if (toolTip() != hint) {
        setToolTip(hint);
    }
    QWidget::mouseMoveEvent(event);
}

void PreviewOverlay::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_previewGeometry.contains(event->pos())) {
        Logger::info(QStringLiteral("ui.preview.close.request source=image-double-click "
                                    "pos=(%1,%2)")
                         .arg(event->position().x())
                         .arg(event->position().y()));
        closePreview();
    }
    event->accept();
}

void PreviewOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        closePreview();
    }
    event->accept();
}

void PreviewOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuildProjectionCache();
    if (m_open && !m_pixmap.isNull()) {
        const bool continueClosing = m_closing;
        m_animationGroup->stop();
        if (continueClosing) {
            animateTo(m_previewGeometry, m_sourceGeometry, m_backdropProgress, 0.0, true);
        } else {
            m_closing = false;
            setPreviewGeometry(targetRectForPixmap(m_pixmap));
        }
    }
}

QRect PreviewOverlay::targetRectForPixmap(const QPixmap& pixmap) const {
    const int shortestEdge = std::max(1, std::min(width(), height()));
    const int margin = std::clamp(qRound(shortestEdge * 0.08), 44, 84);
    const QSize maxSize(std::max(1, width() - margin * 2),
                        std::max(1, height() - margin * 2));
    const QSize scaled = pixmap.size().scaled(maxSize, Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
}

void PreviewOverlay::animateTo(const QRect& from, const QRect& to, qreal fromProgress,
                               qreal toProgress, bool closing) {
    m_closing = closing;
    const int geometryDuration = closing ? 220 : 280;
    const int backdropDuration = closing ? 190 : 240;
    const QEasingCurve easing = closing ? QEasingCurve::InOutCubic : QEasingCurve::OutQuart;

    m_geometryAnimation->setStartValue(from);
    m_geometryAnimation->setEndValue(to);
    m_geometryAnimation->setDuration(geometryDuration);
    m_geometryAnimation->setEasingCurve(easing);
    m_backdropAnimation->setStartValue(fromProgress);
    m_backdropAnimation->setEndValue(toProgress);
    m_backdropAnimation->setDuration(backdropDuration);
    m_backdropAnimation->setEasingCurve(easing);
    m_animationGroup->start();
}

void PreviewOverlay::rebuildProjectionCache() {
    m_projectionCache = {};
    if (m_backgroundMode != PreviewBackgroundMode::SelfImage || m_pixmap.isNull() ||
        size().isEmpty()) {
        return;
    }

    QSize cacheSize = size();
    cacheSize.scale(QSize(kProjectionMaximumExtent, kProjectionMaximumExtent),
                    Qt::KeepAspectRatio);
    cacheSize = cacheSize.expandedTo(QSize(1, 1));
    QPixmap cover = m_pixmap.scaled(cacheSize, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
    const QRect crop((cover.width() - cacheSize.width()) / 2,
                     (cover.height() - cacheSize.height()) / 2, cacheSize.width(),
                     cacheSize.height());
    cover = cover.copy(crop);

    // A two-stage downsample approximates a dual-Kawase blur without a graphics-effect scene or
    // per-frame filtering. The cache stays below roughly half a megabyte at normal window sizes.
    const QSize blurSize(std::max(1, cacheSize.width() / kProjectionDownsampleFactor),
                         std::max(1, cacheSize.height() / kProjectionDownsampleFactor));
    m_projectionCache =
        cover.scaled(blurSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .scaled(cacheSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    Logger::debug(QStringLiteral("ui.preview.background.cache mode=self-image size=(%1,%2) "
                                 "source=(%3,%4)")
                      .arg(cacheSize.width())
                      .arg(cacheSize.height())
                      .arg(m_pixmap.width())
                      .arg(m_pixmap.height()));
}

void PreviewOverlay::setInputBarrierActive(bool active) {
    if (m_inputBarrierActive == active) {
        return;
    }
    m_inputBarrierActive = active;
    if (!qApp) {
        return;
    }
    if (active) {
        qApp->installEventFilter(this);
        grabMouse();
        grabKeyboard();
    } else {
        if (QWidget::mouseGrabber() == this) {
            releaseMouse();
        }
        if (QWidget::keyboardGrabber() == this) {
            releaseKeyboard();
        }
        qApp->removeEventFilter(this);
    }
}

bool PreviewOverlay::isOverlayDispatchObject(const QObject* object) const {
    // Native pointer input reaches the top-level QWindow before Qt dispatches the translated
    // QWidget event. Blocking that first stage prevents this overlay from ever receiving real
    // mouse input, even though direct QTest delivery still appears to work.
    const QWidget* topLevel = window();
    if (topLevel && object == topLevel->windowHandle()) {
        return true;
    }
    for (const QObject* current = object; current; current = current->parent()) {
        if (current == this) {
            return true;
        }
    }
    return false;
}

} // namespace tlm
