#include "pages/EditPage.h"

#include "logging/Logger.h"
#include "preview/PreviewOverlay.h"
#include "tier/ImageEditDialog.h"
#include "tier/ImageGalleryPopover.h"
#include "tier/TierBoardWidget.h"

#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace tlm {

namespace {
constexpr int kPopoverArrowHeight = 12;
constexpr int kPopoverArrowCenterX = 46;
constexpr int kContentTitleBarHeight = 54;
constexpr int kTierBoardOuterMargin = 16;
constexpr auto kDefaultBackgroundIconPath = ":/images/app-icon.png";
constexpr qreal kDefaultBackgroundIconVisibility = 0.22;

int platformPopoverRadius() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return 16;
#else
    return 10;
#endif
}

QColor popoverSurfaceColor(const QPalette& palette) {
    return palette.color(QPalette::Base).lightness() < 96 ? QColor(31, 35, 53)
                                                          : QColor(250, 251, 253);
}

QColor popoverStrokeColor(const QPalette& palette) {
    return palette.color(QPalette::Base).lightness() < 96 ? QColor(68, 76, 110)
                                                          : QColor(210, 218, 232);
}

QPainterPath roundedPopoverPath(const QRectF& bubbleRect, qreal arrowCenterX, qreal arrowHeight,
                                qreal radius) {
    QPainterPath bubble;
    bubble.addRoundedRect(bubbleRect, radius, radius);

    if (bubbleRect.width() <= radius * 2.0 || arrowHeight <= 0.0) {
        return bubble;
    }

    constexpr qreal kArrowHalfWidth = 18.0;
    constexpr qreal kTipHalfWidth = 4.2;
    constexpr qreal kBodyOverlap = 2.5;
    const qreal minCenter = bubbleRect.left() + radius + kArrowHalfWidth;
    const qreal maxCenter = bubbleRect.right() - radius - kArrowHalfWidth;
    const qreal centerX = qBound(minCenter, arrowCenterX, maxCenter);
    const qreal baseY = bubbleRect.top() + kBodyOverlap;
    const qreal tipY = bubbleRect.top() - arrowHeight + 0.5;

    // The arrow overlaps the body, then both paths are unioned so the shared edge is never stroked.
    QPainterPath arrow;
    arrow.moveTo(centerX - kArrowHalfWidth, baseY);
    arrow.cubicTo(centerX - 13.0, baseY, centerX - 9.5, tipY + 6.0,
                  centerX - kTipHalfWidth, tipY + 2.8);
    arrow.quadTo(centerX, tipY - 0.4, centerX + kTipHalfWidth, tipY + 2.8);
    arrow.cubicTo(centerX + 9.5, tipY + 6.0, centerX + 13.0, baseY,
                  centerX + kArrowHalfWidth, baseY);
    arrow.closeSubpath();

    return bubble.united(arrow);
}

class BackgroundPopover final : public QDialog {
public:
    explicit BackgroundPopover(QWidget* parent = nullptr)
        : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setObjectName(QStringLiteral("BackgroundPopover"));
    }

    void setOutsideDismissSuspended(bool suspended) {
        if (m_outsideDismissSuspended == suspended) {
            return;
        }
        m_outsideDismissSuspended = suspended;
        Logger::debug(QStringLiteral("tier.edit.background.popover.dismiss.suspended value=%1").arg(suspended));
    }

    void placeBelow(const QRect& globalAnchorRect) {
        m_anchorGlobalRect = globalAnchorRect;
        adjustSize();
        const QSize popupSize = sizeHint().expandedTo(size());
        const int anchorCenterX = globalAnchorRect.isValid()
                                      ? globalAnchorRect.center().x()
                                      : parentWidget()->mapToGlobal(QPoint(parentWidget()->width() - 46, 0)).x();
        QPoint topLeft = globalAnchorRect.isValid()
                             ? QPoint(globalAnchorRect.center().x() - kPopoverArrowCenterX,
                                      globalAnchorRect.bottom() + 8)
                             : parentWidget()->mapToGlobal(QPoint(parentWidget()->width() - popupSize.width() - 22,
                                                                  54));
        QScreen* screen = globalAnchorRect.isValid() ? QGuiApplication::screenAt(globalAnchorRect.center())
                                                     : QGuiApplication::screenAt(topLeft);
        if (screen) {
            const QRect available = screen->availableGeometry().adjusted(10, 10, -10, -10);
            topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - popupSize.width()));
            topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - popupSize.height()));
        }
        const int radius = platformPopoverRadius();
        m_arrowCenterX = qBound(radius + 14, anchorCenterX - topLeft.x(), popupSize.width() - radius - 14);
        move(topLeft);
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        Q_UNUSED(watched);
        if (!isVisible() || m_outsideDismissSuspended || event->type() != QEvent::MouseButtonPress) {
            return false;
        }

        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (geometry().contains(mouseEvent->globalPosition().toPoint())) {
            return false;
        }

        Logger::info(QStringLiteral("tier.edit.background.cancel reason=outside-click"));
        reject();
        return true;
    }

    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        if (!m_eventFilterInstalled && qApp) {
            qApp->installEventFilter(this);
            m_eventFilterInstalled = true;
        }
    }

    void hideEvent(QHideEvent* event) override {
        if (m_eventFilterInstalled && qApp) {
            qApp->removeEventFilter(this);
            m_eventFilterInstalled = false;
        }
        QDialog::hideEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int radius = platformPopoverRadius();
        const QRectF bubbleRect = QRectF(rect()).adjusted(0.5, kPopoverArrowHeight + 0.5, -0.5, -0.5);
        const QPainterPath path =
            roundedPopoverPath(bubbleRect, m_arrowCenterX, kPopoverArrowHeight, radius);

        QColor fill = popoverSurfaceColor(palette());
        fill.setAlpha(255);
        QColor stroke = popoverStrokeColor(palette());
        stroke.setAlpha(palette().color(QPalette::Base).lightness() < 96 ? 210 : 150);
        painter.fillPath(path, fill);
        painter.setPen(QPen(stroke, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

private:
    int m_arrowCenterX{kPopoverArrowCenterX};
    QRect m_anchorGlobalRect;
    bool m_outsideDismissSuspended{false};
    bool m_eventFilterInstalled{false};
};

class BackgroundPreviewWidget final : public QWidget {
public:
    explicit BackgroundPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(118);
        setMinimumWidth(320);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setPreview(const QString& imagePath, bool centeredIcon, qreal visibility,
                    const QString& defaultText) {
        if (m_imagePath != imagePath) {
            m_imagePath = imagePath;
            m_pixmap = QPixmap(m_imagePath);
        }
        m_centeredIcon = centeredIcon;
        m_visibility = qBound<qreal>(0.0, visibility, 1.0);
        m_defaultText = defaultText;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform |
                               QPainter::TextAntialiasing);

        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        painter.setClipPath(clip);
        painter.fillPath(clip, palette().color(QPalette::AlternateBase));

        if (!m_pixmap.isNull() && !rect().isEmpty()) {
            painter.setOpacity(m_visibility);
            if (m_centeredIcon) {
                painter.drawPixmap(centeredIconTargetRect(rect()), m_pixmap, QRectF(m_pixmap.rect()));
            } else {
                painter.drawPixmap(rect(), m_pixmap, sourceRectForTarget(rect().size()));
            }
            painter.setOpacity(1.0);
        } else {
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(rect().adjusted(12, 8, -12, -8), Qt::AlignCenter, m_defaultText);
        }

        painter.setClipping(false);
        QColor stroke = palette().color(QPalette::Mid);
        stroke.setAlpha(120);
        painter.setPen(QPen(stroke, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }

private:
    QRectF centeredIconTargetRect(const QRect& targetRect) const {
        const qreal shortSide = qMax<qreal>(1.0, qMin(targetRect.width(), targetRect.height()));
        const qreal upperSide = qMax<qreal>(24.0, shortSide - 18.0);
        const qreal lowerSide = qMin<qreal>(48.0, upperSide);
        const qreal side = qBound<qreal>(lowerSide, shortSide * 0.58, upperSide);
        const QPointF center = QRectF(targetRect).center();
        return QRectF(center.x() - side / 2.0, center.y() - side / 2.0, side, side);
    }

    QRect sourceRectForTarget(const QSize& targetSize) const {
        if (m_pixmap.isNull() || targetSize.isEmpty()) {
            return {};
        }
        const QSize sourceSize = m_pixmap.size();
        const qreal targetRatio = static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
        const qreal sourceRatio = static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
        if (sourceRatio > targetRatio) {
            const int cropWidth = qRound(sourceSize.height() * targetRatio);
            return QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
        }
        const int cropHeight = qRound(sourceSize.width() / targetRatio);
        return QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(), cropHeight);
    }

    QString m_imagePath;
    QPixmap m_pixmap;
    QString m_defaultText;
    qreal m_visibility{1.0};
    bool m_centeredIcon{false};
};

QString resolvedCanvasImagePath(const TierProject& project, const QString& storedPath) {
    if (storedPath.isEmpty()) {
        return {};
    }
    if (storedPath.startsWith(QStringLiteral(":/")) || storedPath.startsWith(QStringLiteral("qrc:/"))) {
        return storedPath;
    }
    const QFileInfo info(storedPath);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }
    if (!project.filePath.isEmpty()) {
        return QDir(QFileInfo(project.filePath).absolutePath()).filePath(storedPath);
    }
    return storedPath;
}

qreal canvasBackgroundVisibility(const QJsonObject& canvas) {
    const bool hasCustomBackground = !canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty();
    const qreal fallback = hasCustomBackground ? 1.0 : kDefaultBackgroundIconVisibility;
    return qBound<qreal>(
        0.0,
        canvas.value(QStringLiteral("backgroundVisibility"))
            .toDouble(canvas.value(QStringLiteral("backgroundImageOpacity")).toDouble(
                canvas.value(QStringLiteral("backgroundOpacity")).toDouble(fallback))),
        1.0);
}

void applyDialogLineEditTheme(QLineEdit* edit) {
    if (!edit) {
        return;
    }
    if (edit->palette().color(QPalette::Base).lightness() >= 96) {
        edit->setStyleSheet({});
        return;
    }
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:palette(alternate-base);color:palette(window-text);"
        "border:1px solid palette(mid);border-radius:8px;padding:6px 8px;}"
        "QLineEdit:hover{border-color:palette(midlight);}"
        "QLineEdit:focus{border-color:palette(highlight);}"));
}
} // namespace

EditPage::EditPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                   AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
                   QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_recentProjects(recentProjects),
      m_assetManager(assetManager),
      m_thumbnailCache(thumbnailCache),
      m_settings(settings),
      m_exporter(new TierListExporter(assetManager, this)),
      m_project(TierProject::createUntitled()) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    buildUi();
    refreshUi();

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_backgroundPreviewActive && m_settings && m_settings->autosaveEnabled() && m_project.dirty &&
            !m_project.filePath.isEmpty()) {
            saveProject();
        }
    });
    const int interval = (m_settings ? m_settings->autosaveIntervalMinutes() : 3) * 60 * 1000;
    m_autosaveTimer->start(interval);
}

QString EditPage::displayTitle() const {
    return QStringLiteral("%1%2").arg(m_project.name, m_project.dirty ? QStringLiteral(" *") : QString());
}

bool EditPage::newProject() {
    if (!confirmSaveIfDirty()) {
        return false;
    }
    setProject(TierProject::createUntitled());
    return true;
}

bool EditPage::openProjectFromDialog() {
    if (!confirmSaveIfDirty()) {
        return false;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), QString(), tr("TierListMaker Projects (*.tlmproject)"));
    return path.isEmpty() ? false : openProject(path);
}

bool EditPage::openProject(const QString& filePath) {
    auto result = m_repository->openProject(filePath);
    if (!result) {
        showError(tr("Open Failed"), result.error());
        return false;
    }
    setProject(result.takeValue());
    m_recentProjects->addOrUpdate(m_project);
    emit projectOpened(filePath);
    return true;
}

bool EditPage::saveProject() {
    if (m_project.filePath.isEmpty()) {
        return saveProjectAs();
    }
    return saveProjectToPath(m_project.filePath);
}

bool EditPage::saveProjectAs() {
    const QString path = chooseSavePath();
    if (path.isEmpty()) {
        return false;
    }
    return saveProjectToPath(path);
}

void EditPage::renameProject(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_project.name) {
        emit titleChanged(displayTitle());
        return;
    }
    m_project.name = trimmed;
    Logger::info(QStringLiteral("tier.edit.project.rename name=\"%1\"").arg(trimmed));
    markDirty();
}

void EditPage::resetRows() {
    if (QMessageBox::question(this, tr("Reset Rows"),
                              tr("Reset rows to S/A/B/C/D and remove row assignments from images?")) != QMessageBox::Yes) {
        return;
    }
    m_project.resetDefaultRows();
    m_selectedImageId.clear();
    Logger::info(QStringLiteral("tier.edit.rows.reset"));
    markDirty();
    refreshUi();
}

void EditPage::importImagesFromDialog() {
    const QStringList files = chooseImageImportFiles(this);
    if (!files.isEmpty()) {
        importImages(files);
    }
}

void EditPage::importImages(const QStringList& filePaths) {
    ImageImportBehavior behavior = m_settings ? m_settings->importBehavior()
                                              : ImageImportBehavior::CopyIntoProject;
    if (behavior == ImageImportBehavior::AskEveryTime) {
        const int choice =
            QMessageBox::question(this, tr("Import Images"),
                                  tr("Copy imported images into the project folder?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        behavior = choice == QMessageBox::Yes ? ImageImportBehavior::CopyIntoProject
                                              : ImageImportBehavior::ReferenceOriginal;
    }
    auto result = m_assetManager->importImages(m_project, filePaths, behavior);
    if (!result) {
        showError(tr("Import Failed"), result.error());
        return;
    }
    if (!result.value().isEmpty()) {
        m_selectedImageId = result.value().last();
        markDirty();
        refreshUi();
    }
}

void EditPage::exportProjectFromDialog() {
    ExportOptions options;
    options.scale = m_settings ? m_settings->defaultExportScale() : 2;
    const QString format = m_settings ? m_settings->defaultExportFormat() : QStringLiteral("png");
    options.format = ExportOptions::formatFromSuffix(format);
    const QString suggested =
        QFileInfo(m_project.suggestedFileName()).completeBaseName() + QStringLiteral(".") +
        ExportOptions::suffixForFormat(options.format);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Tier List"), suggested, tr("PNG (*.png);;JPEG (*.jpg);;PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    auto result = m_exporter->exportProject(m_project, path, options);
    if (!result) {
        showError(tr("Export Failed"), result.error());
    }
}

void EditPage::configureBackground(const QRect& anchorGlobalRect) {
    const QJsonObject originalCanvas = m_project.canvas;
    const bool originalDirty = m_project.dirty;
    const QDateTime originalUpdatedAt = m_project.updatedAt;
    QString selectedPath = resolvedCanvasImagePath(
        m_project, originalCanvas.value(QStringLiteral("backgroundImagePath")).toString());
    qreal backgroundVisibility = canvasBackgroundVisibility(originalCanvas);
    bool clearBackground = selectedPath.isEmpty();
    m_backgroundPreviewActive = true;

    BackgroundPopover dialog(this);
    dialog.setPalette(qApp ? qApp->palette() : palette());
    dialog.setWindowTitle(tr("Tier List Background"));
    dialog.setFixedWidth(380);
    dialog.setStyleSheet(QStringLiteral(
        "QLabel{color:palette(window-text);}"
        "QPushButton{border:1px solid palette(mid);border-radius:8px;"
        "padding:7px 12px;background:palette(alternate-base);}"
        "QPushButton:hover{background:palette(midlight);}"
        "QPushButton:pressed{background:palette(midlight);}"
        "QSlider::groove:horizontal{height:4px;border-radius:2px;background:palette(mid);}"
        "QSlider::sub-page:horizontal{height:4px;border-radius:2px;background:palette(highlight);}"
        "QSlider::add-page:horizontal{height:4px;border-radius:2px;background:palette(alternate-base);}"
        "QSlider::handle:horizontal{width:16px;height:16px;margin:-6px 0px;"
        "border-radius:8px;background:palette(window);border:1px solid palette(mid);}"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, kPopoverArrowHeight + 18, 18, 16);
    layout->setSpacing(12);

    auto* preview = new BackgroundPreviewWidget(&dialog);

    auto* pathLabel = new QLabel(&dialog);
    pathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);

    auto updatePreview = [&]() {
        const QPixmap pixmap(selectedPath);
        if (!clearBackground && !pixmap.isNull()) {
            preview->setPreview(selectedPath, false, backgroundVisibility, tr("Default Background"));
            pathLabel->setText(QFileInfo(selectedPath).fileName());
        } else {
            preview->setPreview(QString::fromUtf8(kDefaultBackgroundIconPath), true,
                                backgroundVisibility, tr("Default Background"));
            pathLabel->setText(tr("Default Background"));
        }

        m_project.canvas = originalCanvas;
        if (!clearBackground && !selectedPath.isEmpty()) {
            m_project.canvas.insert(QStringLiteral("backgroundImagePath"), selectedPath);
        } else {
            m_project.canvas.remove(QStringLiteral("backgroundImagePath"));
        }
        if (!clearBackground && !selectedPath.isEmpty()) {
            m_project.canvas.insert(QStringLiteral("backgroundVisibility"), backgroundVisibility);
        } else {
            m_project.canvas.insert(QStringLiteral("backgroundVisibility"), backgroundVisibility);
        }
        m_project.canvas.remove(QStringLiteral("backgroundImageOpacity"));
        m_project.canvas.remove(QStringLiteral("backgroundOpacity"));
        m_project.canvas.remove(QStringLiteral("tierListOpacity"));
        m_project.canvas.remove(QStringLiteral("imagesVisible"));
        m_project.canvas.insert(QStringLiteral("previewImagesHidden"), true);
        m_project.dirty = originalDirty;
        m_project.updatedAt = originalUpdatedAt;
        if (m_board) {
            m_board->refreshVisuals();
        }
    };

    auto* choose = new QPushButton(tr("Choose Image"), &dialog);
    auto* clear = new QPushButton(tr("Use Default"), &dialog);
    auto* actions = new QHBoxLayout;
    actions->addWidget(choose);
    actions->addWidget(clear);
    actions->addStretch();

    auto* opacityLabel = new QLabel(&dialog);
    opacityLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* opacitySlider = new QSlider(Qt::Horizontal, &dialog);
    opacitySlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(qRound(backgroundVisibility * 100.0));
    auto updateOpacityLabel = [&]() {
        opacityLabel->setText(tr("Background visibility: %1%").arg(qRound(backgroundVisibility * 100.0)));
    };
    updateOpacityLabel();

    layout->addWidget(preview);
    layout->addWidget(pathLabel);
    layout->addLayout(actions);
    layout->addWidget(opacityLabel);
    layout->addWidget(opacitySlider);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    connect(choose, &QPushButton::clicked, &dialog, [&]() {
        const QStringList filters = m_assetManager ? m_assetManager->supportedNameFilters()
                                                   : QStringList{tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")};
        dialog.setOutsideDismissSuspended(true);
        const QString imagePath = QFileDialog::getOpenFileName(&dialog, tr("Choose Background Image"),
                                                               QString(), filters.join(QStringLiteral(";;")));
        dialog.setOutsideDismissSuspended(false);
        if (imagePath.isEmpty()) {
            Logger::debug(QStringLiteral("tier.edit.background.preview.choose.cancel"));
            return;
        }
        selectedPath = imagePath;
        clearBackground = false;
        Logger::info(QStringLiteral("tier.edit.background.preview path=\"%1\"").arg(imagePath));
        updatePreview();
    });
    connect(clear, &QPushButton::clicked, &dialog, [&]() {
        selectedPath.clear();
        clearBackground = true;
        backgroundVisibility = kDefaultBackgroundIconVisibility;
        {
            const QSignalBlocker blocker(opacitySlider);
            opacitySlider->setValue(qRound(backgroundVisibility * 100.0));
        }
        updateOpacityLabel();
        Logger::info(QStringLiteral("tier.edit.background.preview.clear"));
        updatePreview();
    });
    connect(opacitySlider, &QSlider::valueChanged, &dialog, [&](int value) {
        backgroundVisibility = static_cast<qreal>(value) / 100.0;
        updateOpacityLabel();
        updatePreview();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    updatePreview();
    dialog.placeBelow(anchorGlobalRect);
    if (dialog.exec() != QDialog::Accepted) {
        m_project.canvas = originalCanvas;
        m_project.dirty = originalDirty;
        m_project.updatedAt = originalUpdatedAt;
        m_backgroundPreviewActive = false;
        if (m_board) {
            m_board->refreshVisuals();
        }
        emit dirtyChanged(m_project.dirty);
        emit titleChanged(displayTitle());
        Logger::info(QStringLiteral("tier.edit.background.cancel"));
        return;
    }

    m_project.canvas = originalCanvas;
    m_project.dirty = originalDirty;
    m_project.updatedAt = originalUpdatedAt;

    const QString previousPath = originalCanvas.value(QStringLiteral("backgroundImagePath")).toString();
    const qreal previousBackgroundVisibility = canvasBackgroundVisibility(originalCanvas);
    const bool hasLegacyCanvasKeys = originalCanvas.contains(QStringLiteral("backgroundOpacity")) ||
                                     originalCanvas.contains(QStringLiteral("backgroundImageOpacity")) ||
                                     originalCanvas.contains(QStringLiteral("tierListOpacity")) ||
                                     originalCanvas.contains(QStringLiteral("imagesVisible")) ||
                                     originalCanvas.contains(QStringLiteral("previewImagesHidden"));
    bool changed = hasLegacyCanvasKeys;

    if (clearBackground || selectedPath.isEmpty()) {
        changed = changed || !previousPath.isEmpty() ||
                  !qFuzzyCompare(previousBackgroundVisibility + 1.0, backgroundVisibility + 1.0) ||
                  (originalCanvas.contains(QStringLiteral("backgroundVisibility")) &&
                   qFuzzyCompare(backgroundVisibility + 1.0, kDefaultBackgroundIconVisibility + 1.0)) ||
                  originalCanvas.contains(QStringLiteral("backgroundImageOpacity"));
        m_project.canvas.remove(QStringLiteral("backgroundImagePath"));
        if (qFuzzyCompare(backgroundVisibility + 1.0, kDefaultBackgroundIconVisibility + 1.0)) {
            m_project.canvas.remove(QStringLiteral("backgroundVisibility"));
        } else {
            m_project.canvas.insert(QStringLiteral("backgroundVisibility"), backgroundVisibility);
        }
    } else {
        const QString previousResolved = resolvedCanvasImagePath(m_project, previousPath);
        if (!previousPath.isEmpty() && QFileInfo(previousResolved).absoluteFilePath() ==
                                           QFileInfo(selectedPath).absoluteFilePath()) {
            m_project.canvas.insert(QStringLiteral("backgroundImagePath"), previousPath);
        } else {
            ImageImportBehavior behavior = m_settings ? m_settings->importBehavior()
                                                      : ImageImportBehavior::CopyIntoProject;
            if (behavior == ImageImportBehavior::AskEveryTime) {
                const int choice = QMessageBox::question(this, tr("Background Image"),
                                                         tr("Copy the background image into the project assets?"),
                                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                behavior = choice == QMessageBox::Yes ? ImageImportBehavior::CopyIntoProject
                                                      : ImageImportBehavior::ReferenceOriginal;
            }
            auto imported = m_assetManager->importCanvasImage(
                m_project, selectedPath, QStringLiteral("backgroundImagePath"), behavior);
            if (!imported) {
                m_project.canvas = originalCanvas;
                m_project.dirty = originalDirty;
                m_project.updatedAt = originalUpdatedAt;
                m_backgroundPreviewActive = false;
                showError(tr("Background Image"), imported.error());
                refreshUi();
                return;
            }
            changed = changed || previousPath != imported.value();
        }
        changed = changed ||
                  !qFuzzyCompare(previousBackgroundVisibility + 1.0, backgroundVisibility + 1.0);
        m_project.canvas.insert(QStringLiteral("backgroundVisibility"), backgroundVisibility);
    }

    m_project.canvas.remove(QStringLiteral("backgroundImageOpacity"));
    m_project.canvas.remove(QStringLiteral("backgroundOpacity"));
    m_project.canvas.remove(QStringLiteral("tierListOpacity"));
    m_project.canvas.remove(QStringLiteral("imagesVisible"));
    m_project.canvas.remove(QStringLiteral("previewImagesHidden"));
    m_backgroundPreviewActive = false;
    if (changed) {
        markDirty();
    } else {
        refreshUi();
    }
    if (m_board) {
        m_board->refreshVisuals();
    }
    Logger::info(QStringLiteral("tier.edit.background.apply hasImage=%1 backgroundVisibility=%2 previewImagesHidden=false")
                     .arg(!m_project.canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty())
                     .arg(backgroundVisibility, 0, 'f', 2));
}

void EditPage::deleteSelectedImage() {
    if (m_selectedImageId.isEmpty()) {
        return;
    }
    const int choice = QMessageBox::question(this, tr("Remove Image"),
                                            tr("Remove the selected image from this project?"),
                                            QMessageBox::Yes | QMessageBox::Cancel,
                                            QMessageBox::Cancel);
    if (choice != QMessageBox::Yes) {
        return;
    }
    removeImageFromRows(m_selectedImageId);
    auto it = std::remove_if(m_project.images.begin(), m_project.images.end(),
                             [this](const TierImage& image) { return image.id == m_selectedImageId; });
    m_project.images.erase(it, m_project.images.end());
    m_selectedImageId.clear();
    markDirty();
    refreshUi();
}

void EditPage::previewSelectedImage() {
    if (m_previewOverlay->isOpen()) {
        m_previewOverlay->closePreview();
        return;
    }
    const QPixmap pixmap = pixmapForImage(m_selectedImageId);
    if (!pixmap.isNull()) {
        QRect source = m_board ? m_board->imageSourceRect(m_selectedImageId) : QRect();
        if (!source.isValid() && m_galleryPopover) {
            source = m_galleryPopover->imageSourceRect(m_selectedImageId);
        }
        if (!source.isValid()) {
            const QRect fallback(width() / 2 - 20, height() / 2 - 20, 40, 40);
            source = QRect(mapTo(window(), fallback.topLeft()), fallback.size());
        }
        Logger::info(QStringLiteral("tier.edit.preview.request source=space imageId=%1 rect=(%2,%3,%4,%5)")
                         .arg(m_selectedImageId)
                         .arg(source.x())
                         .arg(source.y())
                         .arg(source.width())
                         .arg(source.height()));
        m_previewOverlay->openPreview(source, pixmap);
    }
}

bool EditPage::confirmSaveIfDirty() {
    if (!m_project.dirty) {
        return true;
    }
    const int choice = QMessageBox::warning(this, tr("Unsaved Changes"),
                                            tr("Save changes to \"%1\"?").arg(m_project.name),
                                            QMessageBox::Save | QMessageBox::Discard |
                                                QMessageBox::Cancel,
                                            QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        return saveProject();
    }
    return true;
}

void EditPage::setTierFocusMode(bool enabled) {
    if (m_tierFocusMode == enabled) {
        return;
    }
    m_tierFocusMode = enabled;
    Logger::info(QStringLiteral("tier.edit.focus.mode enabled=%1").arg(enabled));

    if (enabled) {
        if (m_rootLayout) {
            m_rootLayout->setContentsMargins(0, 0, 0, 0);
            m_rootLayout->setSpacing(0);
        }
        if (m_galleryPopover) {
            m_galleryPopover->close();
        }
    } else {
        if (m_rootLayout) {
            m_rootLayout->setContentsMargins(kTierBoardOuterMargin, kContentTitleBarHeight,
                                             kTierBoardOuterMargin, kTierBoardOuterMargin);
            m_rootLayout->setSpacing(0);
        }
    }

    layoutOverlays();
    updateGeometry();
    update();
}

void EditPage::toggleMissionControlMode() {
    if (m_board) {
        m_board->toggleMissionControlMode();
    }
}

void EditPage::toggleGallery(const QRect& anchorGlobalRect) {
    if (m_galleryPopover) {
        Logger::debug(QStringLiteral("tier.gallery.popover.close reason=toggle"));
        m_galleryPopover->close();
        return;
    }

    auto* popover = new ImageGalleryPopover(this);
    popover->setAttribute(Qt::WA_DeleteOnClose);
    popover->setData(&m_project, m_assetManager, m_thumbnailCache, m_selectedImageId);
    connect(popover, &QObject::destroyed, this, [this]() { m_galleryPopover = nullptr; });
    connect(popover, &ImageGalleryPopover::importRequested, this, [this]() {
        QPointer<ImageGalleryPopover> guard = m_galleryPopover;
        if (guard) {
            guard->setOutsideDismissSuspended(true);
        }
        QWidget* dialogParent = guard ? static_cast<QWidget*>(guard.data()) : static_cast<QWidget*>(this);
        const QStringList files = chooseImageImportFiles(dialogParent);
        if (guard) {
            guard->setOutsideDismissSuspended(false);
            guard->raise();
            guard->activateWindow();
        }
        if (!files.isEmpty()) {
            importImages(files);
        } else {
            Logger::debug(QStringLiteral("tier.gallery.import.cancel popoverAlive=%1")
                              .arg(guard != nullptr));
        }
    });
    connect(popover, &ImageGalleryPopover::imageFilesDropped, this, &EditPage::importImages);
    connect(popover, &ImageGalleryPopover::dragActiveChanged, this, [](bool active) {
        Logger::debug(QStringLiteral("tier.gallery.drag.active active=%1").arg(active));
    });
    connect(popover, &ImageGalleryPopover::imageSelected, this, [this](const QString& imageId) {
        m_selectedImageId = imageId;
        refreshUi();
    });
    connect(popover, &ImageGalleryPopover::imagePreviewRequested, this,
            [this](const QString& imageId, const QRect& source) {
                m_selectedImageId = imageId;
                const QPixmap pixmap = pixmapForImage(imageId);
                if (!pixmap.isNull()) {
                    m_previewOverlay->openPreview(source, pixmap);
                }
                refreshUi();
            });
    connect(popover, &ImageGalleryPopover::imageEditRequested, this, &EditPage::editImage);
    connect(popover, &ImageGalleryPopover::imageRemoveRequested, this, &EditPage::removeImageFromGallery);

    m_galleryPopover = popover;
    popover->placeBelow(anchorGlobalRect);
    popover->show();
    Logger::info(QStringLiteral("tier.gallery.popover.open images=%1").arg(m_project.images.size()));
}

void EditPage::toggleGalleryMissionControlMode(const QRect& sourceGlobalRect) {
    if (m_board) {
        m_board->toggleGalleryMissionControlMode(sourceGlobalRect);
    }
}

void EditPage::layoutOverlays() {
    if (m_previewOverlay) {
        m_previewOverlay->raise();
    }
}

void EditPage::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        previewSelectedImage();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedImage();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EditPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_previewOverlay) {
        m_previewOverlay->setGeometry(rect());
    }
    layoutOverlays();
}

void EditPage::buildUi() {
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(kTierBoardOuterMargin, kContentTitleBarHeight,
                                     kTierBoardOuterMargin, kTierBoardOuterMargin);
    m_rootLayout->setSpacing(0);

    m_board = new TierBoardWidget(this);
    auto* boardShadow = new QGraphicsDropShadowEffect(m_board);
    // Match the reference TierListMaker section list elevation: concentrated, centered, and dark.
    boardShadow->setBlurRadius(20);
    boardShadow->setOffset(0, 0);
    boardShadow->setColor(QColor(0, 0, 0, 247));
    m_board->setGraphicsEffect(boardShadow);
    m_rootLayout->addWidget(m_board, 1);
    if (m_settings) {
        const auto applyBlankAreaActions = [this]() {
            if (!m_board || !m_settings) {
                return;
            }
            m_board->setBlankAreaActions(m_settings->blankDoubleClickAction(),
                                         m_settings->blankLongPressAction());
        };
        applyBlankAreaActions();
        connect(m_settings, &AppSettings::blankDoubleClickActionChanged, this,
                [applyBlankAreaActions](BlankAreaAction) { applyBlankAreaActions(); });
        connect(m_settings, &AppSettings::blankLongPressActionChanged, this,
                [applyBlankAreaActions](BlankAreaAction) { applyBlankAreaActions(); });
    }

    m_previewOverlay = new PreviewOverlay(this);
    m_previewOverlay->setGeometry(rect());

    connect(m_board, &TierBoardWidget::imageDropped, this, &EditPage::moveImageToRow);
    connect(m_board, &TierBoardWidget::rowMovedToIndex, this, &EditPage::moveRowToIndex);
    connect(m_board, &TierBoardWidget::rowEditRequested, this, &EditPage::editTierRow);
    connect(m_board, &TierBoardWidget::galleryMissionControlRequested, this,
            &EditPage::galleryMissionControlRequested);
    connect(m_board, &TierBoardWidget::imageEditRequested, this, &EditPage::editImage);
    connect(m_board, &TierBoardWidget::imageRemoveFromTierRowRequested, this,
            &EditPage::removeImageFromTierRow);
    connect(m_board, &TierBoardWidget::imageRemoveFromGalleryRequested, this,
            &EditPage::removeImageFromGallery);
    connect(m_board, &TierBoardWidget::imageSelected, this, [this](const QString& imageId) {
        if (m_selectedImageId == imageId) {
            return;
        }
        m_selectedImageId = imageId;
        if (m_board) {
            m_board->setSelectedImageId(imageId);
        }
        if (m_galleryPopover) {
            m_galleryPopover->setSelectedImageId(imageId);
        }
    });
    connect(m_board, &TierBoardWidget::imagePreviewRequested, this,
            [this](const QString& imageId, const QRect& source) {
                m_selectedImageId = imageId;
                const QPixmap pixmap = pixmapForImage(imageId);
                if (!pixmap.isNull()) {
                    m_previewOverlay->openPreview(source, pixmap);
                }
            });

    layoutOverlays();
}

void EditPage::refreshUi() {
    m_board->setData(&m_project, m_assetManager, m_thumbnailCache, m_selectedImageId);
    if (m_galleryPopover) {
        m_galleryPopover->setData(&m_project, m_assetManager, m_thumbnailCache, m_selectedImageId);
    }
    emit titleChanged(displayTitle());
    emit dirtyChanged(m_project.dirty);
    emit resetRowsAvailableChanged(hasImagesInRows());
}

void EditPage::markDirty() {
    m_project.touch();
    emit dirtyChanged(true);
    emit titleChanged(displayTitle());
    emit resetRowsAvailableChanged(hasImagesInRows());
}

void EditPage::setProject(TierProject project) {
    m_project = std::move(project);
    m_selectedImageId.clear();
    m_thumbnailCache->clear();
    refreshUi();
}

void EditPage::showError(const QString& title, const Error& error) {
    QMessageBox::critical(this, title,
                          error.details.isEmpty() ? error.message
                                                  : QStringLiteral("%1\n\n%2").arg(error.message, error.details));
}

QString EditPage::chooseSavePath() {
    QString suggested = m_project.filePath;
    if (suggested.isEmpty()) {
        suggested = m_project.suggestedFileName();
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), suggested,
                                                tr("TierListMaker Projects (*.tlmproject)"));
    if (!path.isEmpty() && !path.endsWith(QStringLiteral(".tlmproject"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".tlmproject");
    }
    return path;
}

QStringList EditPage::chooseImageImportFiles(QWidget* dialogParent) {
    const QString filter = m_assetManager
                               ? m_assetManager->supportedNameFilters().join(QStringLiteral(";;"))
                               : tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
    const QStringList files = QFileDialog::getOpenFileNames(dialogParent ? dialogParent : this,
                                                            tr("Import Images"), QString(), filter);
    Logger::debug(QStringLiteral("tier.edit.images.import.dialog.finish count=%1 parent=%2")
                      .arg(files.size())
                      .arg(dialogParent ? dialogParent->metaObject()->className() : "null"));
    return files;
}

void EditPage::moveImageToRow(const QString& imageId, const QString& rowId, int index) {
    TierImage* image = m_project.imageById(imageId);
    TierRow* row = m_project.rowById(rowId);
    if (!image || !row) {
        Logger::warn(QStringLiteral("tier.edit.image.move.to.row rejected imageId=%1 rowId=%2 imageFound=%3 rowFound=%4")
                         .arg(imageId, rowId)
                         .arg(image != nullptr)
                         .arg(row != nullptr));
        return;
    }

    const std::optional<QString> previousRowId = image->assignedTierRowId;
    if (previousRowId.has_value() && *previousRowId == rowId) {
        const int previousIndex = static_cast<int>(row->imageIds.indexOf(imageId));
        if (previousIndex >= 0 && previousIndex < index) {
            --index;
        }
    }

    removeImageFromRows(imageId);
    image->assignedTierRowId = rowId;
    index = qBound(0, index, static_cast<int>(row->imageIds.size()));
    row->imageIds.insert(index, imageId);
    const int rowImageCount = static_cast<int>(row->imageIds.size());
    m_project.normalizeOrdering();
    m_selectedImageId = imageId;
    Logger::info(QStringLiteral("tier.edit.image.move.to.row imageId=%1 rowId=%2 index=%3 rowImageCount=%4")
                     .arg(imageId, rowId)
                     .arg(index)
                     .arg(rowImageCount));
    markDirty();
    refreshUi();
}

void EditPage::editImage(const QString& imageId) {
    TierImage* image = m_project.imageById(imageId);
    if (!image) {
        Logger::warn(QStringLiteral("tier.edit.image.edit rejected imageId=%1 reason=missing").arg(imageId));
        return;
    }

    const QPixmap pixmap = pixmapForImage(imageId);
    if (pixmap.isNull()) {
        Logger::warn(QStringLiteral("tier.edit.image.edit rejected imageId=%1 reason=invalidPixmap").arg(imageId));
        return;
    }

    ImageEditDialog dialog(*image, pixmap, this);
    if (dialog.exec() != QDialog::Accepted) {
        Logger::debug(QStringLiteral("tier.edit.image.edit.cancel imageId=%1").arg(imageId));
        return;
    }

    const QString nextName = dialog.displayName().isEmpty() ? image->originalFileName : dialog.displayName();
    const QRectF nextCrop = dialog.cropRect();
    const bool changed = image->displayName != nextName ||
                         qAbs(image->cropRect.x() - nextCrop.x()) > 0.0005 ||
                         qAbs(image->cropRect.y() - nextCrop.y()) > 0.0005 ||
                         qAbs(image->cropRect.width() - nextCrop.width()) > 0.0005 ||
                         qAbs(image->cropRect.height() - nextCrop.height()) > 0.0005;
    if (!changed) {
        Logger::debug(QStringLiteral("tier.edit.image.edit.noop imageId=%1").arg(imageId));
        return;
    }

    image->displayName = nextName;
    image->cropRect = nextCrop;
    m_selectedImageId = imageId;
    Logger::info(QStringLiteral("tier.edit.image.edit.apply imageId=%1 name=\"%2\" crop=(%3,%4,%5,%6)")
                     .arg(imageId, nextName)
                     .arg(nextCrop.x(), 0, 'f', 4)
                     .arg(nextCrop.y(), 0, 'f', 4)
                     .arg(nextCrop.width(), 0, 'f', 4)
                     .arg(nextCrop.height(), 0, 'f', 4));
    markDirty();
    refreshUi();
}

void EditPage::removeImageFromTierRow(const QString& imageId) {
    TierImage* image = m_project.imageById(imageId);
    if (!image) {
        Logger::warn(QStringLiteral("tier.edit.image.remove.from.row rejected imageId=%1 reason=missing").arg(imageId));
        return;
    }

    const QString previousRowId = image->assignedTierRowId.value_or(QString());
    removeImageFromRows(imageId);
    image->assignedTierRowId.reset();
    m_project.normalizeOrdering();
    m_selectedImageId = imageId;
    Logger::info(QStringLiteral("tier.edit.image.remove.from.row imageId=%1 previousRowId=%2")
                     .arg(imageId, previousRowId));
    markDirty();
    refreshUi();
}

void EditPage::removeImageFromGallery(const QString& imageId) {
    const qsizetype before = m_project.images.size();
    removeImageFromRows(imageId);
    m_project.images.erase(std::remove_if(m_project.images.begin(), m_project.images.end(),
                                          [&](const TierImage& image) { return image.id == imageId; }),
                           m_project.images.end());
    if (m_project.images.size() == before) {
        Logger::warn(QStringLiteral("tier.edit.image.remove.from.gallery rejected imageId=%1 reason=missing")
                         .arg(imageId));
        return;
    }
    if (m_selectedImageId == imageId) {
        m_selectedImageId.clear();
    }
    m_project.normalizeOrdering();
    Logger::info(QStringLiteral("tier.edit.image.remove.from.gallery imageId=%1 remaining=%2")
                     .arg(imageId)
                     .arg(m_project.images.size()));
    markDirty();
    refreshUi();
}

void EditPage::moveRowToIndex(const QString& rowId, int destinationIndex) {
    int sourceIndex = -1;
    for (int i = 0; i < m_project.rows.size(); ++i) {
        if (m_project.rows[i].id == rowId) {
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex < 0) {
        Logger::warn(QStringLiteral("tier.edit.row.move rejected rowId=%1 sourceRow=-1 destination=%2")
                         .arg(rowId)
                         .arg(destinationIndex));
        return;
    }

    destinationIndex = qBound(0, destinationIndex, static_cast<int>(m_project.rows.size()) - 1);
    if (sourceIndex == destinationIndex) {
        Logger::debug(QStringLiteral("tier.edit.row.move noop rowId=%1 source=%2 destination=%3")
                          .arg(rowId)
                          .arg(sourceIndex)
                          .arg(destinationIndex));
        return;
    }

    TierRow moved = m_project.rows.takeAt(sourceIndex);
    m_project.rows.insert(qBound(0, destinationIndex, static_cast<int>(m_project.rows.size())), moved);
    for (int i = 0; i < static_cast<int>(m_project.rows.size()); ++i) {
        m_project.rows[i].order = i;
    }
    m_project.normalizeOrdering();
    Logger::info(QStringLiteral("tier.edit.row.move rowId=%1 source=%2 destination=%3")
                     .arg(rowId)
                     .arg(sourceIndex)
                     .arg(destinationIndex));
    markDirty();
    refreshUi();
}

void EditPage::editTierRow(const QString& rowId) {
    TierRow* row = m_project.rowById(rowId);
    if (!row) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Tier"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* labelEdit = new QLineEdit(row->label, &dialog);
    applyDialogLineEditTheme(labelEdit);
    QColor selectedColor = row->color;
    auto* colorButton = new QPushButton(selectedColor.name(QColor::HexRgb), &dialog);
    colorButton->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:#111111;}").arg(
        selectedColor.name(QColor::HexRgb)));
    connect(colorButton, &QPushButton::clicked, &dialog, [&]() {
        const QColor next = QColorDialog::getColor(selectedColor, &dialog, tr("Tier Color"));
        if (!next.isValid()) {
            return;
        }
        selectedColor = next;
        colorButton->setText(selectedColor.name(QColor::HexRgb));
        colorButton->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:#111111;}").arg(
            selectedColor.name(QColor::HexRgb)));
    });

    form->addRow(tr("Label"), labelEdit);
    form->addRow(tr("Color"), colorButton);
    layout->addLayout(form);

    auto* deleteRow = new QPushButton(tr("Delete"), &dialog);
    auto* actions = new QHBoxLayout;
    actions->addWidget(deleteRow);
    actions->addStretch();
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(deleteRow, &QPushButton::clicked, &dialog, [&]() {
        if (m_project.rows.size() <= 1) {
            QMessageBox::information(&dialog, tr("Delete Row"), tr("At least one row is required."));
            return;
        }
        if (QMessageBox::question(&dialog, tr("Delete Row"),
                                  tr("Delete this row and remove its image assignments?")) != QMessageBox::Yes) {
            return;
        }
        if (TierRow* deleted = m_project.rowById(rowId)) {
            for (const QString& imageId : deleted->imageIds) {
                if (TierImage* image = m_project.imageById(imageId)) {
                    image->assignedTierRowId.reset();
                }
            }
        }
        auto it = std::remove_if(m_project.rows.begin(), m_project.rows.end(),
                                 [&](const TierRow& item) { return item.id == rowId; });
        m_project.rows.erase(it, m_project.rows.end());
        for (int rowIndex = 0; rowIndex < static_cast<int>(m_project.rows.size()); ++rowIndex) {
            m_project.rows[rowIndex].order = rowIndex;
        }
        m_project.normalizeOrdering();
        markDirty();
        refreshUi();
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    row = m_project.rowById(rowId);
    if (!row) {
        return;
    }
    const QString nextLabel = labelEdit->text().trimmed();
    if (!nextLabel.isEmpty() && (row->label != nextLabel || row->color != selectedColor)) {
        row->label = nextLabel;
        row->color = selectedColor;
        markDirty();
        refreshUi();
    }
}

bool EditPage::saveProjectToPath(const QString& filePath) {
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const QString previousPath = m_project.filePath;
    const bool pathChanged = previousPath.isEmpty() || QFileInfo(previousPath).absoluteFilePath() != absolutePath;
    if (!m_project.dirty && !pathChanged) {
        Logger::debug(QStringLiteral("tier.edit.project.save.noop path=\"%1\" reason=clean").arg(absolutePath));
        emit dirtyChanged(false);
        emit titleChanged(displayTitle());
        return true;
    }

    m_project.filePath = absolutePath;
    m_project.updatedAt = QDateTime::currentDateTimeUtc();

    auto migrate = m_assetManager->migrateSessionAssets(m_project, absolutePath);
    if (!migrate) {
        m_project.filePath = previousPath;
        showError(tr("Save Failed"), migrate.error());
        return false;
    }
    const bool assetsMigrated = migrate.value();

    auto result = m_repository->saveProject(m_project, absolutePath);
    if (!result) {
        m_project.filePath = previousPath;
        showError(tr("Save Failed"), result.error());
        return false;
    }

    auto recentResult = m_recentProjects->addOrUpdate(m_project);
    if (!recentResult) {
        QMessageBox::warning(this, tr("Recent Projects"),
                             recentResult.error().details.isEmpty()
                                 ? recentResult.error().message
                                 : QStringLiteral("%1\n\n%2").arg(recentResult.error().message,
                                                                   recentResult.error().details));
    }

    emit dirtyChanged(false);
    emit titleChanged(displayTitle());
    emit projectSaved();
    Logger::info(QStringLiteral("tier.edit.project.save path=\"%1\" assetsMigrated=%2 pathChanged=%3")
                     .arg(absolutePath)
                     .arg(assetsMigrated)
                     .arg(pathChanged));
    return true;
}

void EditPage::removeImageFromRows(const QString& imageId) {
    for (TierRow& row : m_project.rows) {
        row.imageIds.removeAll(imageId);
    }
}

bool EditPage::hasImagesInRows() const {
    return std::any_of(m_project.rows.cbegin(), m_project.rows.cend(), [](const TierRow& row) {
        return !row.imageIds.isEmpty();
    });
}

QPixmap EditPage::pixmapForImage(const QString& imageId) const {
    const TierImage* image = m_project.imageById(imageId);
    if (!image) {
        return {};
    }
    return QPixmap(m_assetManager->resolvedImagePath(m_project, *image));
}

} // namespace tlm
