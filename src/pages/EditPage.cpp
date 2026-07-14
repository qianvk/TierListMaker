#include "pages/EditPage.h"

#include "widgets/DestructiveActionDialog.h"

#include "logging/Logger.h"
#include "pages/ProjectLocationDialog.h"
#include "preview/PreviewOverlay.h"
#include "theme/Theme.h"
#include "tier/ImageEditDialog.h"
#include "tier/ImageGalleryPopover.h"
#include "tier/TierBoardWidget.h"
#include "tier/TierRowEditDialog.h"
#include "window/AppDialog.h"

#include <QApplication>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#if !defined(Q_OS_WIN)
#include <QGraphicsDropShadowEffect>
#endif
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <vkui/core/VkIcon.h>
#include <vkui/widgets/overlays/VkPopover.h>

namespace tlm {

namespace {
constexpr int kContentTitleBarHeight = 54;
constexpr int kTierBoardOuterMargin = 16;
constexpr auto kDefaultBackgroundIconPath = ":/images/app-icon.png";
constexpr qreal kDefaultBackgroundIconVisibility = 0.22;

struct BuiltInTemplate {
    QString id;
    QString name;
    TierProject project;
};

constexpr auto kBuiltInLocalizedTemplateId = "builtin:localized";
constexpr auto kBuiltInSabcdTemplateId = "builtin:sabcd";
constexpr auto kBuiltInChineseTemplateId = "builtin:cn-hit-to-trash";
constexpr auto kCustomTemplatePrefix = "custom:";

QString templateFileStem(const QString& value) {
    QString stem = value.trimmed();
    stem.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")),
                 QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    stem = stem.trimmed();
    while (stem.endsWith(u'.')) {
        stem.chop(1);
    }
    return stem.isEmpty() ? QObject::tr("Untitled Template") : stem;
}

QVector<TierRow> rowsFromLabels(const QStringList& labels) {
    static const QVector<QColor> colors = {
        QColor(QStringLiteral("#ff7b7b")), QColor(QStringLiteral("#ffc36b")),
        QColor(QStringLiteral("#ffe17d")), QColor(QStringLiteral("#8bdc8b")),
        QColor(QStringLiteral("#82b7ff")),
    };
    QVector<TierRow> rows;
    rows.reserve(labels.size());
    for (int index = 0; index < labels.size(); ++index) {
        rows.append(
            TierRow::makeDefault(labels.at(index), colors.at(index % colors.size()), index));
    }
    return rows;
}

TierProject templateProjectFromRows(const QString& name, const QStringList& labels) {
    TierProject project = TierProject::createUntitled();
    project.name = name;
    project.rows = rowsFromLabels(labels);
    project.images.clear();
    project.filePath.clear();
    project.thumbnailPath.clear();
    project.dirty = true;
    return project;
}

BuiltInTemplate sabcdTemplate() {
    return {QString::fromLatin1(kBuiltInSabcdTemplateId), QObject::tr("S,A,B,C,D"),
            templateProjectFromRows(QObject::tr("S,A,B,C,D"),
                                    {QStringLiteral("S"), QStringLiteral("A"), QStringLiteral("B"),
                                     QStringLiteral("C"), QStringLiteral("D")})};
}

BuiltInTemplate chineseTemplate() {
    return {QString::fromLatin1(kBuiltInChineseTemplateId), QStringLiteral("从夯到拉"),
            templateProjectFromRows(QStringLiteral("从夯到拉"),
                                    {QStringLiteral("夯"), QStringLiteral("顶级"),
                                     QStringLiteral("人上人"), QStringLiteral("NPC"),
                                     QStringLiteral("拉")})};
}

bool prefersChineseTemplate(const AppSettings* settings) {
    const QString language = settings ? settings->language() : QStringLiteral("system");
    if (language.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
        return true;
    }
    if (language != QStringLiteral("system")) {
        return false;
    }
    return QLocale::system().language() == QLocale::Chinese;
}

BuiltInTemplate localizedBuiltInTemplate(const AppSettings* settings) {
    return prefersChineseTemplate(settings) ? chineseTemplate() : sabcdTemplate();
}

BuiltInTemplate builtInTemplateForId(const QString& id, const AppSettings* settings) {
    if (id == QString::fromLatin1(kBuiltInChineseTemplateId)) {
        return chineseTemplate();
    }
    if (id == QString::fromLatin1(kBuiltInSabcdTemplateId)) {
        return sabcdTemplate();
    }
    return localizedBuiltInTemplate(settings);
}

class BackgroundPopover final : public QObject {
public:
    enum Result { Rejected, Accepted };

    explicit BackgroundPopover(QWidget* parent = nullptr)
        : QObject(parent), m_popover(std::make_unique<vkui::VkPopover>(parent)),
          m_content(new QWidget) {
        m_content->setObjectName(QStringLiteral("BackgroundPopoverContent"));
        m_popover->setPreferredPlacement(vkui::VkPopoverPlacement::Below);
        m_popover->setContentWidget(m_content);
        connect(m_popover.get(), &vkui::VkPopover::closed, this, [this]() {
            if (m_loop) {
                m_loop->quit();
            }
        });
    }

    QWidget* contentWidget() const {
        return m_content;
    }
    void setFixedWidth(int width) {
        m_content->setFixedWidth(width);
    }

    void setOutsideDismissSuspended(bool suspended) {
        if (m_outsideDismissSuspended == suspended) {
            return;
        }
        m_outsideDismissSuspended = suspended;
        const auto normalPolicy = vkui::VkPopoverClosePolicyFlag::OutsideClick |
                                  vkui::VkPopoverClosePolicyFlag::EscapeKey |
                                  vkui::VkPopoverClosePolicyFlag::AnchorDestroyed |
                                  vkui::VkPopoverClosePolicyFlag::WindowDeactivated;
        m_popover->setClosePolicy(suspended ? vkui::VkPopoverClosePolicyFlag::AnchorDestroyed
                                            : normalPolicy);
        Logger::debug(QStringLiteral("tier.edit.background.popover.dismiss.suspended value=%1")
                          .arg(suspended));
    }

    int execFor(QWidget* anchor) {
        if (!anchor) {
            return Rejected;
        }
        m_result = Rejected;
        QEventLoop loop;
        m_loop = &loop;
        QPointer<QWidget> anchorGuard(anchor);
        m_anchor = anchor;
        anchor->installEventFilter(this);
        m_popover->openFor(anchor);
        if (!m_popover->isOpen()) {
            if (anchorGuard) {
                anchorGuard->removeEventFilter(this);
            }
            m_anchor = nullptr;
            m_loop = nullptr;
            Logger::warn(
                QStringLiteral("tier.edit.background.popover.open rejected invalid-anchor=1"));
            return Rejected;
        }
        loop.exec(QEventLoop::DialogExec);
        if (anchorGuard) {
            anchorGuard->removeEventFilter(this);
        }
        m_anchor = nullptr;
        m_loop = nullptr;
        return m_result;
    }

    void accept() {
        m_result = Accepted;
        m_popover->closeAnimated();
    }

    void reject() {
        m_result = Rejected;
        m_popover->closeAnimated();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_anchor.data() && event && event->type() == QEvent::MouseButtonPress) {
            reject();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::unique_ptr<vkui::VkPopover> m_popover;
    QWidget* m_content{nullptr};
    QPointer<QWidget> m_anchor;
    QEventLoop* m_loop{nullptr};
    int m_result{Rejected};
    bool m_outsideDismissSuspended{false};
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
        const ThemeTokens& colors = activeThemeTokens();
        painter.fillPath(clip, colors.elevatedBackground);

        if (!m_pixmap.isNull() && !rect().isEmpty()) {
            painter.setOpacity(m_visibility);
            if (m_centeredIcon) {
                painter.drawPixmap(centeredIconTargetRect(rect()), m_pixmap,
                                   QRectF(m_pixmap.rect()));
            } else {
                painter.drawPixmap(rect(), m_pixmap, sourceRectForTarget(rect().size()));
            }
            painter.setOpacity(1.0);
        } else {
            painter.setPen(colors.primaryText);
            painter.drawText(rect().adjusted(12, 8, -12, -8), Qt::AlignCenter, m_defaultText);
        }

        painter.setClipping(false);
        painter.setPen(QPen(colors.border, 1));
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
        const qreal targetRatio =
            static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
        const qreal sourceRatio =
            static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
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
    if (storedPath.startsWith(QStringLiteral(":/")) ||
        storedPath.startsWith(QStringLiteral("qrc:/"))) {
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
    const bool hasCustomBackground =
        !canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty();
    const qreal fallback = hasCustomBackground ? 1.0 : kDefaultBackgroundIconVisibility;
    return qBound<qreal>(
        0.0,
        canvas.value(QStringLiteral("backgroundVisibility"))
            .toDouble(
                canvas.value(QStringLiteral("backgroundImageOpacity"))
                    .toDouble(
                        canvas.value(QStringLiteral("backgroundOpacity")).toDouble(fallback))),
        1.0);
}

} // namespace

EditPage::EditPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                   AssetManager* assetManager, ThumbnailCache* thumbnailCache,
                   AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_recentProjects(recentProjects),
      m_assetManager(assetManager), m_thumbnailCache(thumbnailCache), m_settings(settings),
      m_exporter(new TierListExporter(assetManager, this)),
      m_project(TierProject::createUntitled()) {
    m_project = createProjectFromDefaultTemplate();
    m_project.dirty = false;
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    buildUi();
    refreshUi();

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_backgroundPreviewActive && m_settings && m_settings->autosaveEnabled() &&
            m_project.dirty) {
            autosaveCurrentProject();
        }
    });
    const int interval = (m_settings ? m_settings->autosaveIntervalMinutes() : 3) * 60 * 1000;
    m_autosaveTimer->start(interval);
}

QString EditPage::displayTitle() const {
    return m_project.name;
}

bool EditPage::newProject() {
    if (!confirmSaveIfDirty()) {
        return false;
    }

    TierProject project = createProjectFromDefaultTemplate();
    QString projectName = project.name;
    const QString path = uniqueDefaultProjectPath(&projectName);
    project.name = projectName;
    setProject(std::move(project));
    Logger::info(QStringLiteral("tier.edit.project.create path=\"%1\"").arg(path));
    return saveProjectToPath(path);
}

bool EditPage::openProjectFromDialog() {
    if (!confirmSaveIfDirty()) {
        return false;
    }
    const QString directory = m_settings ? m_settings->defaultProjectDirectory() : QString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), directory,
                                                      tr("TierListMaker Projects (*.tlmproject)"));
    if (path.isEmpty()) {
        return false;
    }
    if (m_settings) {
        m_settings->setDefaultProjectDirectory(QFileInfo(path).absolutePath());
    }
    return openProject(path);
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
        QString projectName = m_project.name;
        const QString path = uniqueDefaultProjectPath(&projectName);
        if (projectName != m_project.name) {
            m_project.name = projectName;
            m_project.dirty = true;
        }
        return saveProjectToPath(path);
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

void EditPage::showTemplateMenu(QWidget* anchor) {
    if (m_templatePopover && m_templatePopover->isOpen()) {
        if (anchor && m_templatePopoverAnchor == anchor) {
            m_templatePopover->closeAnimated();
            return;
        }
        m_templatePopover->closeAnimated();
    }
    if (!anchor) {
        return;
    }
    closeTransientPopovers();

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("TemplatePopoverContent"));
    content->setFixedWidth(360);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Templates"), content);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* popover = new vkui::VkPopover(this);
    m_templatePopover = popover;
    popover->setPreferredPlacement(vkui::VkPopoverPlacement::Below);
    popover->setContentWidget(content);
    QPointer<vkui::VkPopover> popoverGuard(popover);
    QPointer<QWidget> anchorGuard(anchor);
    connect(popover, &vkui::VkPopover::closed, this, [this, popover]() {
        if (m_templatePopover == popover) {
            m_templatePopover = nullptr;
            m_templatePopoverAnchor = nullptr;
        }
        popover->deleteLater();
    });
    m_templatePopoverAnchor = anchor;

    auto addSection = [&](const QString& text) {
        auto* label = new QLabel(text, content);
        label->setObjectName(QStringLiteral("TemplatePopoverSection"));
        label->setStyleSheet(QStringLiteral("QLabel#TemplatePopoverSection{color:palette(mid);"
                                            "font-size:11px;font-weight:600;}"));
        layout->addWidget(label);
    };

    const QString localizedBuiltInId = localizedBuiltInTemplate(m_settings).id;
    auto isDefaultTemplateId = [this, localizedBuiltInId](const QString& id) {
        const QString configuredDefault = m_settings ? m_settings->defaultTemplateId() : QString();
        if (id == localizedBuiltInId) {
            return configuredDefault.isEmpty() ||
                   configuredDefault == QString::fromLatin1(kBuiltInLocalizedTemplateId) ||
                   configuredDefault == localizedBuiltInId;
        }
        return !configuredDefault.isEmpty() && configuredDefault == id;
    };

    struct DefaultTemplateButtonBinding {
        QString id;
        QPointer<QToolButton> button;
    };
    auto defaultButtons = std::make_shared<QVector<DefaultTemplateButtonBinding>>();
    auto updateDefaultButton = [this, isDefaultTemplateId](QToolButton* button, const QString& id) {
        if (!button) {
            return;
        }
        const bool active = isDefaultTemplateId(id);
        button->setIcon(
            vkui::icon(active ? vkui::VkSymbol::Checkmark : vkui::VkSymbol::DefaultTemplate,
                       active ? vkui::VkIconRole::Accent : vkui::VkIconRole::Secondary));
        button->setToolTip(active ? tr("Default template") : tr("Set as default template"));
    };
    auto refreshDefaultButtons = [defaultButtons, updateDefaultButton]() {
        for (const DefaultTemplateButtonBinding& binding : *defaultButtons) {
            updateDefaultButton(binding.button, binding.id);
        }
    };

    auto addTemplateRow = [&](const QString& id, const QString& name, const QIcon& icon,
                              auto applyCallback, auto deleteCallback) {
        auto* row = new QWidget(content);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto* applyButton = new QPushButton(icon, name, row);
        applyButton->setObjectName(QStringLiteral("TemplateItemButton"));
        applyButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        applyButton->setMinimumHeight(34);
        applyButton->setToolTip(tr("Apply template"));
        rowLayout->addWidget(applyButton, 1);
        connect(applyButton, &QPushButton::clicked, this, [=]() {
            if (popoverGuard) {
                popoverGuard->closeAnimated();
            }
            applyCallback();
        });

        auto* defaultButton = new QToolButton(row);
        defaultButton->setAutoRaise(true);
        defaultButton->setIconSize(QSize(16, 16));
        defaultButton->setFixedSize(32, 32);
        updateDefaultButton(defaultButton, id);
        defaultButtons->append({id, defaultButton});
        rowLayout->addWidget(defaultButton);
        connect(defaultButton, &QToolButton::clicked, this, [=]() {
            if (m_settings) {
                m_settings->setDefaultTemplateId(id);
            }
            refreshDefaultButtons();
        });

        if constexpr (!std::is_same_v<std::decay_t<decltype(deleteCallback)>, std::nullptr_t>) {
            auto* deleteButton = new QToolButton(row);
            deleteButton->setAutoRaise(true);
            deleteButton->setIcon(vkui::icon(vkui::VkSymbol::Trash, vkui::VkIconRole::Destructive));
            deleteButton->setIconSize(QSize(16, 16));
            deleteButton->setFixedSize(32, 32);
            deleteButton->setToolTip(tr("Delete template"));
            rowLayout->addWidget(deleteButton);
            connect(deleteButton, &QToolButton::clicked, this, [=]() {
                deleteCallback();
                if (popoverGuard) {
                    popoverGuard->closeAnimated();
                }
                if (anchorGuard) {
                    QTimer::singleShot(180, this, [this, anchorGuard]() {
                        if (anchorGuard) {
                            showTemplateMenu(anchorGuard);
                        }
                    });
                }
            });
        }
        layout->addWidget(row);
    };

    addSection(tr("Built-in"));
    for (const BuiltInTemplate& entry :
         QVector<BuiltInTemplate>{localizedBuiltInTemplate(m_settings)}) {
        addTemplateRow(
            entry.id, entry.name, vkui::icon(vkui::VkSymbol::Templates),
            [this, project = entry.project, name = entry.name]() {
                if (applyTemplateProject(project)) {
                    Logger::info(
                        QStringLiteral("tier.edit.template.apply builtin=\"%1\"").arg(name));
                }
            },
            nullptr);
    }

    addSection(tr("Custom"));
    QDir directory(managedTemplateDirectory());
    const QFileInfoList files =
        directory.entryInfoList({QStringLiteral("*.tlmtemplate")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        auto* empty = new QLabel(tr("No custom templates saved."), content);
        empty->setObjectName(QStringLiteral("TemplatePopoverEmpty"));
        empty->setStyleSheet(QStringLiteral("QLabel#TemplatePopoverEmpty{color:palette(mid);}"));
        layout->addWidget(empty);
    } else {
        for (const QFileInfo& file : files) {
            QString displayName = file.completeBaseName();
            if (auto result = m_repository->openProject(file.absoluteFilePath())) {
                displayName = result.value().name;
            }
            const QString templateId =
                QString::fromLatin1(kCustomTemplatePrefix) + file.absoluteFilePath();
            addTemplateRow(
                templateId, displayName, vkui::icon(vkui::VkSymbol::Document),
                [this, path = file.absoluteFilePath()]() {
                    auto result = m_repository->openProject(path);
                    if (!result) {
                        showError(tr("Apply Template"), result.error());
                        return;
                    }
                    if (applyTemplateProject(result.value())) {
                        Logger::info(
                            QStringLiteral("tier.edit.template.apply path=\"%1\"").arg(path));
                    }
                },
                [this, path = file.absoluteFilePath(), displayName, templateId]() {
                    if (!confirmDestructiveAction(
                            this, tr("Delete Template"),
                            tr("Delete the custom template \"%1\"?").arg(displayName))) {
                        return;
                    }
                    if (!QFile::remove(path)) {
                        QMessageBox::warning(this, tr("Delete Template"),
                                             tr("Could not delete the template file."));
                        return;
                    }
                    if (m_settings && m_settings->defaultTemplateId() == templateId) {
                        m_settings->setDefaultTemplateId({});
                    }
                });
        }
    }

    auto* separator = new QFrame(content);
    separator->setFrameShape(QFrame::HLine);
    layout->addWidget(separator);

    auto* saveCurrent =
        new QPushButton(vkui::icon(vkui::VkSymbol::Save), tr("Save Current Template..."), content);
    auto* importFile = new QPushButton(vkui::icon(vkui::VkSymbol::Download),
                                       tr("Import Template File..."), content);
    auto* exportFile =
        new QPushButton(vkui::icon(vkui::VkSymbol::Share), tr("Export Template File..."), content);
    for (QPushButton* button : {saveCurrent, importFile, exportFile}) {
        button->setMinimumHeight(32);
        layout->addWidget(button);
    }
    connect(saveCurrent, &QPushButton::clicked, this, [this, popoverGuard, anchorGuard]() {
        if (popoverGuard) {
            popoverGuard->closeAnimated();
        }
        saveManagedTemplateFromPrompt(anchorGuard);
    });
    connect(importFile, &QPushButton::clicked, this, [this, popoverGuard]() {
        if (popoverGuard) {
            popoverGuard->closeAnimated();
        }
        applyTemplateFromDialog();
    });
    connect(exportFile, &QPushButton::clicked, this, [this, popoverGuard]() {
        if (popoverGuard) {
            popoverGuard->closeAnimated();
        }
        saveTemplateFromDialog();
    });

    popover->openFor(anchor);
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
    if (!confirmDestructiveAction(
            this, tr("Reset Rows"),
            tr("Reset rows to S/A/B/C/D and remove row assignments from images?"))) {
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
    if (m_project.filePath.isEmpty() && !ensureProjectFile()) {
        return;
    }
    auto result = m_assetManager->importImages(m_project, filePaths);
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
    const QString suggested = QFileInfo(m_project.suggestedFileName()).completeBaseName() +
                              QStringLiteral(".") + ExportOptions::suffixForFormat(options.format);
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Tier List"), suggested,
                                                      tr("PNG (*.png);;JPEG (*.jpg);;PDF (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    auto result = m_exporter->exportProject(m_project, path, options);
    if (!result) {
        showError(tr("Export Failed"), result.error());
    }
}

void EditPage::configureBackground(QWidget* anchor) {
    if (m_backgroundPreviewActive) {
        Logger::debug(QStringLiteral("tier.edit.background.popover.ignore active=1"));
        return;
    }
    closeTransientPopovers();

    const QJsonObject originalCanvas = m_project.canvas;
    const bool originalDirty = m_project.dirty;
    const QDateTime originalUpdatedAt = m_project.updatedAt;
    QString selectedPath = resolvedCanvasImagePath(
        m_project, originalCanvas.value(QStringLiteral("backgroundImagePath")).toString());
    qreal backgroundVisibility = canvasBackgroundVisibility(originalCanvas);
    bool clearBackground = selectedPath.isEmpty();
    m_backgroundPreviewActive = true;

    BackgroundPopover dialog(this);
    dialog.setFixedWidth(380);
    QWidget* popoverContent = dialog.contentWidget();
    auto* layout = new QVBoxLayout(popoverContent);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(12);

    auto* preview = new BackgroundPreviewWidget(popoverContent);

    auto* pathLabel = new QLabel(popoverContent);
    pathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);

    auto updatePreview = [&]() {
        const QPixmap pixmap(selectedPath);
        if (!clearBackground && !pixmap.isNull()) {
            preview->setPreview(selectedPath, false, backgroundVisibility,
                                tr("Default Background"));
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

    auto* choose = new QPushButton(tr("Choose Image"), popoverContent);
    auto* clear = new QPushButton(tr("Use Default"), popoverContent);
    auto* actions = new QHBoxLayout;
    actions->addWidget(choose);
    actions->addWidget(clear);
    actions->addStretch();

    auto* opacityLabel = new QLabel(popoverContent);
    opacityLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* opacitySlider = new QSlider(Qt::Horizontal, popoverContent);
    opacitySlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(qRound(backgroundVisibility * 100.0));
    auto updateOpacityLabel = [&]() {
        opacityLabel->setText(
            tr("Background visibility: %1%").arg(qRound(backgroundVisibility * 100.0)));
    };
    updateOpacityLabel();

    layout->addWidget(preview);
    layout->addWidget(pathLabel);
    layout->addLayout(actions);
    layout->addWidget(opacityLabel);
    layout->addWidget(opacitySlider);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, popoverContent);
    layout->addWidget(buttons);

    connect(choose, &QPushButton::clicked, &dialog, [&]() {
        const QStringList filters =
            m_assetManager ? m_assetManager->supportedNameFilters()
                           : QStringList{tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")};
        dialog.setOutsideDismissSuspended(true);
        const QString imagePath =
            QFileDialog::getOpenFileName(popoverContent, tr("Choose Background Image"), QString(),
                                         filters.join(QStringLiteral(";;")));
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
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &BackgroundPopover::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &BackgroundPopover::reject);

    updatePreview();
    if (dialog.execFor(anchor ? anchor : this) != BackgroundPopover::Accepted) {
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

    const QString previousPath =
        originalCanvas.value(QStringLiteral("backgroundImagePath")).toString();
    const qreal previousBackgroundVisibility = canvasBackgroundVisibility(originalCanvas);
    const bool hasLegacyCanvasKeys =
        originalCanvas.contains(QStringLiteral("backgroundOpacity")) ||
        originalCanvas.contains(QStringLiteral("backgroundImageOpacity")) ||
        originalCanvas.contains(QStringLiteral("tierListOpacity")) ||
        originalCanvas.contains(QStringLiteral("imagesVisible")) ||
        originalCanvas.contains(QStringLiteral("previewImagesHidden"));
    bool changed = hasLegacyCanvasKeys;

    if (clearBackground || selectedPath.isEmpty()) {
        changed =
            changed || !previousPath.isEmpty() ||
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
            if (m_project.filePath.isEmpty() && !ensureProjectFile()) {
                m_project.canvas = originalCanvas;
                m_project.dirty = originalDirty;
                m_project.updatedAt = originalUpdatedAt;
                m_backgroundPreviewActive = false;
                refreshUi();
                return;
            }
            auto imported = m_assetManager->importCanvasImage(
                m_project, selectedPath, QStringLiteral("backgroundImagePath"));
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
    Logger::info(
        QStringLiteral("tier.edit.background.apply hasImage=%1 backgroundVisibility=%2 "
                       "previewImagesHidden=false")
            .arg(
                !m_project.canvas.value(QStringLiteral("backgroundImagePath")).toString().isEmpty())
            .arg(backgroundVisibility, 0, 'f', 2));
}

void EditPage::deleteSelectedImage() {
    if (m_selectedImageId.isEmpty()) {
        return;
    }
    if (!confirmDestructiveAction(this, tr("Remove Image"),
                                  tr("Remove the selected image from this project?"))) {
        return;
    }
    removeImageFromRows(m_selectedImageId);
    auto it =
        std::remove_if(m_project.images.begin(), m_project.images.end(),
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
        Logger::info(
            QStringLiteral("tier.edit.preview.request source=space imageId=%1 rect=(%2,%3,%4,%5)")
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
    const int choice = QMessageBox::warning(
        this, tr("Unsaved Changes"), tr("Save changes to \"%1\"?").arg(m_project.name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
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
            m_galleryPopover->closeAnimated();
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

void EditPage::toggleGallery(QWidget* anchor) {
    if (m_galleryPopover && m_galleryPopover->isOpen()) {
        if (anchor && m_galleryPopoverAnchor == anchor) {
            Logger::debug(QStringLiteral("tier.gallery.popover.close reason=toggle"));
            m_galleryPopover->closeAnimated();
            return;
        }
        Logger::debug(QStringLiteral("tier.gallery.popover.close reason=switch-anchor"));
        m_galleryPopover->closeAnimated();
    }
    closeTransientPopovers();

    if (!m_galleryPopover) {
        auto* popover = new ImageGalleryPopover(this);
        m_galleryPopover = popover;
        connect(popover, &ImageGalleryPopover::importRequested, this, [this]() {
            QPointer<ImageGalleryPopover> guard = m_galleryPopover;
            if (guard) {
                guard->setOutsideDismissSuspended(true);
            }
            QWidget* dialogParent =
                guard ? static_cast<QWidget*>(guard.data()) : static_cast<QWidget*>(this);
            const QStringList files = chooseImageImportFiles(dialogParent);
            if (guard) {
                guard->setOutsideDismissSuspended(false);
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
        connect(popover, &ImageGalleryPopover::imageRemoveRequested, this,
                &EditPage::removeImageFromGallery);
    }

    auto* popover = m_galleryPopover.data();
    popover->setData(&m_project, m_assetManager, m_thumbnailCache, m_selectedImageId);
    popover->openFor(anchor);
    m_galleryPopoverAnchor = anchor;
    Logger::info(
        QStringLiteral("tier.gallery.popover.open images=%1").arg(m_project.images.size()));
}

void EditPage::clearProject() {
    closeTransientPopovers();
    if (m_previewOverlay && m_previewOverlay->isOpen()) {
        m_previewOverlay->closePreview();
    }
    m_backgroundPreviewActive = false;

    TierProject empty = TierProject::createUntitled();
    empty.name = tr("Untitled Tier List");
    empty.filePath.clear();
    empty.images.clear();
    for (TierRow& row : empty.rows) {
        row.imageIds.clear();
    }
    empty.dirty = false;
    setProject(std::move(empty));
    Logger::info(QStringLiteral("tier.edit.project.clear"));
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
#if !defined(Q_OS_WIN)
    auto* boardShadow = new QGraphicsDropShadowEffect(m_board);
    // Match the reference TierListMaker section list elevation: concentrated, centered, and dark.
    boardShadow->setBlurRadius(20);
    boardShadow->setOffset(0, 0);
    boardShadow->setColor(QColor(0, 0, 0, 247));
    m_board->setGraphicsEffect(boardShadow);
#endif
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
    connect(m_board, &TierBoardWidget::rowClearRequested, this, &EditPage::clearTierRowImages);
    connect(m_board, &TierBoardWidget::rowDeleteRequested, this, &EditPage::deleteTierRow);
    connect(m_board, &TierBoardWidget::rowInsertAboveRequested, this,
            [this](const QString& rowId) { insertTierRow(rowId, false); });
    connect(m_board, &TierBoardWidget::rowInsertBelowRequested, this,
            [this](const QString& rowId) { insertTierRow(rowId, true); });
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
                          error.details.isEmpty()
                              ? error.message
                              : QStringLiteral("%1\n\n%2").arg(error.message, error.details));
}

QString EditPage::chooseSavePath() {
    const QString defaultDirectory =
        m_settings ? m_settings->defaultProjectDirectory()
                   : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString parentDirectory = defaultDirectory;
    if (!m_project.filePath.isEmpty()) {
        const QFileInfo projectFile(m_project.filePath);
        const QFileInfo projectFolder(projectFile.absolutePath());
        parentDirectory = projectFolder.fileName().compare(projectFile.completeBaseName(),
                                                           Qt::CaseInsensitive) == 0
                              ? projectFolder.absolutePath()
                              : projectFile.absolutePath();
    }

    ProjectLocationDialog dialog(m_project.name, parentDirectory, defaultDirectory, this);
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    const QString nextName = dialog.projectName();
    if (!nextName.isEmpty() && nextName != m_project.name) {
        m_project.name = nextName;
        m_project.dirty = true;
        emit dirtyChanged(true);
        emit titleChanged(displayTitle());
    }
    if (m_settings && dialog.shouldUseAsDefaultDirectory()) {
        m_settings->setDefaultProjectDirectory(dialog.parentDirectory());
    }
    return dialog.projectFilePath();
}

QString EditPage::chooseTemplatePath(bool saveDialog) {
    const QString directory =
        m_settings ? m_settings->defaultProjectDirectory()
                   : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suffix = QStringLiteral(".tlmtemplate");
    const QString suggested =
        saveDialog
            ? QDir(directory.isEmpty() ? QDir::homePath() : directory)
                  .filePath(QFileInfo(m_project.suggestedFileName()).completeBaseName() + suffix)
            : directory;
    const QString filter = tr("TierListMaker Templates (*.tlmtemplate);;TierListMaker Projects "
                              "(*.tlmproject)");
    QString path =
        saveDialog ? QFileDialog::getSaveFileName(this, tr("Save Template"), suggested, filter)
                   : QFileDialog::getOpenFileName(this, tr("Apply Template"), suggested, filter);
    if (saveDialog && !path.isEmpty() &&
        !path.endsWith(QStringLiteral(".tlmtemplate"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".tlmproject"), Qt::CaseInsensitive)) {
        path += suffix;
    }
    if (!path.isEmpty() && m_settings) {
        m_settings->setDefaultProjectDirectory(QFileInfo(path).absolutePath());
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

void EditPage::closeTransientPopovers() {
    if (m_templatePopover && m_templatePopover->isOpen()) {
        m_templatePopover->closeAnimated();
    }
    if (m_galleryPopover && m_galleryPopover->isOpen()) {
        m_galleryPopover->closeAnimated();
        m_galleryPopoverAnchor = nullptr;
    }
}

bool EditPage::ensureProjectFile() {
    if (!m_project.filePath.isEmpty()) {
        return true;
    }
    QString projectName = m_project.name;
    const QString path = uniqueDefaultProjectPath(&projectName);
    if (projectName != m_project.name) {
        m_project.name = projectName;
        m_project.dirty = true;
    }
    return saveProjectToPath(path);
}

TierProject EditPage::createProjectFromDefaultTemplate() const {
    const QString configured = m_settings ? m_settings->defaultTemplateId() : QString();
    TierProject project = TierProject::createUntitled();
    bool appliedTemplate = false;
    if (configured.startsWith(QString::fromLatin1(kCustomTemplatePrefix)) && m_repository) {
        const QString path = configured.mid(QString::fromLatin1(kCustomTemplatePrefix).size());
        if (auto result = m_repository->openProject(path)) {
            project.rows = result.value().rows;
            project.canvas = result.value().canvas;
            appliedTemplate = true;
        } else if (m_settings) {
            m_settings->setDefaultTemplateId({});
        }
    }
    if (!appliedTemplate) {
        const BuiltInTemplate entry = builtInTemplateForId(configured, m_settings);
        project.rows = entry.project.rows;
        project.canvas = entry.project.canvas;
    }
    for (TierRow& row : project.rows) {
        row.imageIds.clear();
    }
    project.images.clear();
    project.name = tr("Untitled Tier List");
    project.filePath.clear();
    project.thumbnailPath.clear();
    project.normalizeOrdering();
    project.dirty = true;
    return project;
}

QString EditPage::uniqueDefaultProjectPath(QString* projectName) const {
    const QString directory =
        m_settings ? m_settings->defaultProjectDirectory()
                   : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString baseName = projectName && !projectName->trimmed().isEmpty()
                                 ? projectName->trimmed()
                                 : tr("Untitled Tier List");
    auto pathForName = [](const QString& parent, const QString& name) {
        TierProject candidate = TierProject::createUntitled();
        candidate.name = name;
        const QString folder = QFileInfo(candidate.suggestedFileName()).completeBaseName();
        return QDir(QDir(parent).filePath(folder)).filePath(candidate.suggestedFileName());
    };

    QString name = baseName;
    QString path = pathForName(directory, name);
    int suffix = 2;
    while (QFileInfo::exists(path)) {
        name = QStringLiteral("%1 %2").arg(baseName).arg(suffix++);
        path = pathForName(directory, name);
    }
    if (projectName) {
        *projectName = name;
    }
    return path;
}

bool EditPage::autosaveCurrentProject() {
    if (!m_project.dirty) {
        return true;
    }
    if (!m_project.filePath.isEmpty()) {
        return saveProjectToPath(m_project.filePath);
    }

    QString projectName = m_project.name;
    const QString path = uniqueDefaultProjectPath(&projectName);
    if (projectName != m_project.name) {
        m_project.name = projectName;
        m_project.dirty = true;
    }
    return saveProjectToPath(path);
}

TierProject EditPage::templateSnapshot() const {
    TierProject snapshot = m_project;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshot.filePath.clear();
    snapshot.thumbnailPath.clear();
    snapshot.images.clear();
    for (TierRow& row : snapshot.rows) {
        row.imageIds.clear();
    }
    snapshot.dirty = true;
    return snapshot;
}

bool EditPage::saveTemplateFromDialog() {
    const QString path = chooseTemplatePath(true);
    if (path.isEmpty()) {
        return false;
    }
    return saveTemplateToPath(path);
}

bool EditPage::saveTemplateToPath(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    TierProject snapshot = templateSnapshot();
    const QString originalBackground =
        m_project.canvas.value(QStringLiteral("backgroundImagePath")).toString();
    const QString resolvedBackground = resolvedCanvasImagePath(m_project, originalBackground);
    snapshot.canvas.remove(QStringLiteral("backgroundImagePath"));
    snapshot.filePath = absolutePath;

    if (!resolvedBackground.isEmpty()) {
        if (resolvedBackground.startsWith(QStringLiteral(":/")) ||
            resolvedBackground.startsWith(QStringLiteral("qrc:/"))) {
            snapshot.canvas.insert(QStringLiteral("backgroundImagePath"), resolvedBackground);
        } else {
            auto imported = m_assetManager->importCanvasImage(
                snapshot, resolvedBackground, QStringLiteral("backgroundImagePath"));
            if (!imported) {
                showError(tr("Save Template"), imported.error());
                return false;
            }
        }
    }

    auto result = m_repository->saveProject(snapshot, absolutePath);
    if (!result) {
        showError(tr("Save Template"), result.error());
        return false;
    }

    Logger::info(QStringLiteral("tier.edit.template.save path=\"%1\" rows=%2 hasBackground=%3")
                     .arg(absolutePath)
                     .arg(snapshot.rows.size())
                     .arg(!snapshot.canvas.value(QStringLiteral("backgroundImagePath"))
                               .toString()
                               .isEmpty()));
    return true;
}

bool EditPage::saveManagedTemplateFromPrompt(QWidget* reopenAnchor) {
    AppDialog dialog(tr("Save Current Template"), this);
    dialog.setMinimumWidth(420);

    auto* nameEdit = new QLineEdit(m_project.name, &dialog);
    nameEdit->setClearButtonEnabled(true);
    auto* form = new QFormLayout;
    form->addRow(tr("Template name"), nameEdit);
    dialog.contentLayout()->addLayout(form);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
    dialog.contentLayout()->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, tr("Save Template"), tr("Enter a template name."));
            return;
        }
        dialog.accept();
    });
    QTimer::singleShot(0, &dialog, [&]() {
        nameEdit->setFocus(Qt::OtherFocusReason);
        nameEdit->setCursorPosition(nameEdit->text().size());
    });
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString templateName = nameEdit->text().trimmed();
    QDir directory(managedTemplateDirectory());
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        QMessageBox::warning(this, tr("Save Template"),
                             tr("Could not create the template folder."));
        return false;
    }
    const QString path =
        directory.filePath(templateFileStem(templateName) + QStringLiteral(".tlmtemplate"));
    if (QFileInfo::exists(path) &&
        !confirmDestructiveAction(
            this, tr("Replace Template"),
            tr("Replace the existing template \"%1\"?").arg(templateName))) {
        return false;
    }

    TierProject previous = m_project;
    m_project.name = templateName;
    const bool saved = saveTemplateToPath(path);
    m_project = previous;
    refreshUi();
    if (saved && reopenAnchor) {
        QPointer<QWidget> anchor(reopenAnchor);
        QTimer::singleShot(180, this, [this, anchor]() {
            if (anchor) {
                showTemplateMenu(anchor);
            }
        });
    }
    return saved;
}

QString EditPage::managedTemplateDirectory() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir(QDir::homePath()).filePath(QStringLiteral(".tierlistmaker"));
    }
    return QDir(base).filePath(QStringLiteral("templates"));
}

bool EditPage::applyTemplateFromDialog() {
    const QString path = chooseTemplatePath(false);
    if (path.isEmpty()) {
        return false;
    }

    auto result = m_repository->openProject(path);
    if (!result) {
        showError(tr("Apply Template"), result.error());
        return false;
    }

    if (!applyTemplateProject(result.value())) {
        return false;
    }
    Logger::info(QStringLiteral("tier.edit.template.apply path=\"%1\"").arg(path));
    return true;
}

bool EditPage::confirmTemplateApplication() {
    int assignedImageCount = 0;
    for (const TierRow& row : std::as_const(m_project.rows)) {
        assignedImageCount += static_cast<int>(row.imageIds.size());
    }

    return confirmDestructiveAction(
        this, tr("Apply Template"),
        tr("Applying a template replaces the current tiers and background. "
           "%1 image(s) placed in tiers will be moved back to the gallery. "
           "No images will be deleted from the project.")
            .arg(assignedImageCount),
        tr("Apply Template"));
}

bool EditPage::applyTemplateProject(const TierProject& templateProject) {
    if (!confirmTemplateApplication()) {
        return false;
    }
    TierProject previous = m_project;
    QVector<TierRow> rows = templateProject.rows;
    for (TierRow& row : rows) {
        row.imageIds.clear();
    }
    for (TierImage& image : m_project.images) {
        image.assignedTierRowId.reset();
    }

    const QString templateBackground =
        templateProject.canvas.value(QStringLiteral("backgroundImagePath")).toString();
    const QString resolvedBackground = resolvedCanvasImagePath(templateProject, templateBackground);
    QJsonObject nextCanvas = templateProject.canvas;
    nextCanvas.remove(QStringLiteral("backgroundImagePath"));

    m_project.rows = rows;
    m_project.canvas = nextCanvas;
    m_project.normalizeOrdering();

    if (!resolvedBackground.isEmpty()) {
        if (resolvedBackground.startsWith(QStringLiteral(":/")) ||
            resolvedBackground.startsWith(QStringLiteral("qrc:/"))) {
            m_project.canvas.insert(QStringLiteral("backgroundImagePath"), resolvedBackground);
        } else {
            if (m_project.filePath.isEmpty() && !ensureProjectFile()) {
                m_project = previous;
                refreshUi();
                return false;
            }
            auto imported = m_assetManager->importCanvasImage(
                m_project, resolvedBackground, QStringLiteral("backgroundImagePath"));
            if (!imported) {
                m_project = previous;
                showError(tr("Apply Template"), imported.error());
                refreshUi();
                return false;
            }
        }
    }

    markDirty();
    refreshUi();
    return true;
}

void EditPage::moveImageToRow(const QString& imageId, const QString& rowId, int index) {
    TierImage* image = m_project.imageById(imageId);
    TierRow* row = m_project.rowById(rowId);
    if (!image || !row) {
        Logger::warn(QStringLiteral("tier.edit.image.move.to.row rejected imageId=%1 rowId=%2 "
                                    "imageFound=%3 rowFound=%4")
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
    Logger::info(
        QStringLiteral("tier.edit.image.move.to.row imageId=%1 rowId=%2 index=%3 rowImageCount=%4")
            .arg(imageId, rowId)
            .arg(index)
            .arg(rowImageCount));
    markDirty();
    refreshUi();
}

void EditPage::editImage(const QString& imageId) {
    TierImage* image = m_project.imageById(imageId);
    if (!image) {
        Logger::warn(
            QStringLiteral("tier.edit.image.edit rejected imageId=%1 reason=missing").arg(imageId));
        return;
    }

    const QPixmap pixmap = pixmapForImage(imageId);
    if (pixmap.isNull()) {
        Logger::warn(QStringLiteral("tier.edit.image.edit rejected imageId=%1 reason=invalidPixmap")
                         .arg(imageId));
        return;
    }

    ImageEditDialog dialog(*image, pixmap, this);
    if (dialog.exec() != QDialog::Accepted) {
        Logger::debug(QStringLiteral("tier.edit.image.edit.cancel imageId=%1").arg(imageId));
        return;
    }

    const QString nextName =
        dialog.displayName().isEmpty() ? image->originalFileName : dialog.displayName();
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
    Logger::info(
        QStringLiteral("tier.edit.image.edit.apply imageId=%1 name=\"%2\" crop=(%3,%4,%5,%6)")
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
        Logger::warn(
            QStringLiteral("tier.edit.image.remove.from.row rejected imageId=%1 reason=missing")
                .arg(imageId));
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
    m_project.images.erase(
        std::remove_if(m_project.images.begin(), m_project.images.end(),
                       [&](const TierImage& image) { return image.id == imageId; }),
        m_project.images.end());
    if (m_project.images.size() == before) {
        Logger::warn(
            QStringLiteral("tier.edit.image.remove.from.gallery rejected imageId=%1 reason=missing")
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
        Logger::warn(
            QStringLiteral("tier.edit.row.move rejected rowId=%1 sourceRow=-1 destination=%2")
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
    m_project.rows.insert(qBound(0, destinationIndex, static_cast<int>(m_project.rows.size())),
                          moved);
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

    TierRowEditDialog dialog(tr("Edit Tier"), row->label, row->color, tr("Tier name"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    row = m_project.rowById(rowId);
    if (!row) {
        return;
    }
    const QString nextLabel = dialog.labelText();
    const QColor selectedColor = dialog.color();
    if (!nextLabel.isEmpty() && (row->label != nextLabel || row->color != selectedColor)) {
        row->label = nextLabel;
        row->color = selectedColor;
        Logger::info(QStringLiteral("tier.edit.row.update rowId=%1 label=\"%2\" color=%3")
                         .arg(rowId, nextLabel, selectedColor.name(QColor::HexRgb)));
        markDirty();
        refreshUi();
    }
}

void EditPage::clearTierRowImages(const QString& rowId) {
    TierRow* row = m_project.rowById(rowId);
    if (!row || row->imageIds.isEmpty()) {
        return;
    }
    if (!confirmDestructiveAction(this, tr("Clear Tier Images"),
                                  tr("Remove all images from \"%1\"?").arg(row->label))) {
        return;
    }

    const QStringList imageIds = row->imageIds;
    row->imageIds.clear();
    for (const QString& imageId : imageIds) {
        if (TierImage* image = m_project.imageById(imageId)) {
            image->assignedTierRowId.reset();
        }
    }
    m_project.normalizeOrdering();
    Logger::info(
        QStringLiteral("tier.edit.row.clear rowId=%1 count=%2").arg(rowId).arg(imageIds.size()));
    markDirty();
    refreshUi();
}

void EditPage::deleteTierRow(const QString& rowId) {
    if (m_project.rows.size() <= 1) {
        QMessageBox::information(this, tr("Delete Row"), tr("At least one row is required."));
        return;
    }
    const TierRow* row = m_project.rowById(rowId);
    if (!row) {
        return;
    }
    if (!confirmDestructiveAction(
            this, tr("Delete Row"),
            tr("Delete \"%1\" and remove its image assignments?").arg(row->label))) {
        return;
    }

    for (const QString& imageId : row->imageIds) {
        if (TierImage* image = m_project.imageById(imageId)) {
            image->assignedTierRowId.reset();
        }
    }
    auto it = std::remove_if(m_project.rows.begin(), m_project.rows.end(),
                             [&](const TierRow& item) { return item.id == rowId; });
    m_project.rows.erase(it, m_project.rows.end());
    for (int rowIndex = 0; rowIndex < static_cast<int>(m_project.rows.size()); ++rowIndex) {
        m_project.rows[rowIndex].order = rowIndex;
    }
    m_project.normalizeOrdering();
    Logger::info(QStringLiteral("tier.edit.row.delete rowId=%1 remaining=%2")
                     .arg(rowId)
                     .arg(m_project.rows.size()));
    markDirty();
    refreshUi();
}

void EditPage::insertTierRow(const QString& rowId, bool below) {
    int referenceIndex = -1;
    QColor referenceColor(QStringLiteral("#8bdc8b"));
    for (int index = 0; index < m_project.rows.size(); ++index) {
        if (m_project.rows.at(index).id == rowId) {
            referenceIndex = index;
            referenceColor = m_project.rows.at(index).color;
            break;
        }
    }
    if (referenceIndex < 0) {
        return;
    }

    TierRowEditDialog dialog(below ? tr("Insert Row Below") : tr("Insert Row Above"), {},
                             referenceColor, tr("New Tier"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString label = dialog.labelText().isEmpty() ? tr("New Tier") : dialog.labelText();
    const int insertionIndex = referenceIndex + (below ? 1 : 0);
    TierRow row = TierRow::makeDefault(label, dialog.color(), insertionIndex);
    m_project.rows.insert(qBound(0, insertionIndex, m_project.rows.size()), row);
    for (int rowIndex = 0; rowIndex < static_cast<int>(m_project.rows.size()); ++rowIndex) {
        m_project.rows[rowIndex].order = rowIndex;
    }
    m_project.normalizeOrdering();
    Logger::info(QStringLiteral("tier.edit.row.insert rowId=%1 reference=%2 index=%3 label=\"%4\"")
                     .arg(row.id, rowId)
                     .arg(insertionIndex)
                     .arg(label));
    markDirty();
    refreshUi();
}

bool EditPage::saveProjectToPath(const QString& filePath) {
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const QString previousPath = m_project.filePath;
    const bool pathChanged =
        previousPath.isEmpty() || QFileInfo(previousPath).absoluteFilePath() != absolutePath;
    if (pathChanged && QFileInfo::exists(absolutePath)) {
        QMessageBox::warning(this, tr("Save Project"),
                             tr("A project with this name already exists."));
        return false;
    }
    if (!m_project.dirty && !pathChanged) {
        Logger::debug(QStringLiteral("tier.edit.project.save.noop path=\"%1\" reason=clean")
                          .arg(absolutePath));
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
        QMessageBox::warning(
            this, tr("Recent Projects"),
            recentResult.error().details.isEmpty()
                ? recentResult.error().message
                : QStringLiteral("%1\n\n%2")
                      .arg(recentResult.error().message, recentResult.error().details));
    }

    emit dirtyChanged(false);
    emit titleChanged(displayTitle());
    emit projectSaved();
    Logger::info(
        QStringLiteral("tier.edit.project.save path=\"%1\" assetsMigrated=%2 pathChanged=%3")
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
    return std::any_of(m_project.rows.cbegin(), m_project.rows.cend(),
                       [](const TierRow& row) { return !row.imageIds.isEmpty(); });
}

QPixmap EditPage::pixmapForImage(const QString& imageId) const {
    const TierImage* image = m_project.imageById(imageId);
    if (!image) {
        return {};
    }
    return QPixmap(m_assetManager->resolvedImagePath(m_project, *image));
}

} // namespace tlm
