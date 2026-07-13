#include "pages/ProjectsPage.h"

#include "logging/Logger.h"
#include "pages/ProjectLocationDialog.h"
#include "persistence/ProjectRepository.h"
#include "platform/Platform.h"
#include "settings/AppSettings.h"
#include "theme/Theme.h"
#include "tier/ImageEditDialog.h"
#include "tier/TierImage.h"

#include <QAbstractListModel>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>

#include <vkui/core/VkIcon.h>
#include <vkui/widgets/VkComboBox.h>

namespace tlm {

namespace {
enum ProjectRole {
    FilePathRole = Qt::UserRole,
    UpdatedAtRole,
    CreatedAtRole,
    RowCountRole,
    ImageCountRole,
    ExistsRole,
    CoverPathRole,
    BackgroundPathRole
};

QString resolveProjectRelativePath(const QString& projectPath, const QString& storedPath) {
    if (storedPath.isEmpty()) {
        return {};
    }
    const QFileInfo stored(storedPath);
    if (stored.isAbsolute()) {
        return stored.absoluteFilePath();
    }
    return QDir(QFileInfo(projectPath).absolutePath()).filePath(storedPath);
}

QString backgroundPathFromProjectFile(const QString& projectPath) {
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }
    const QJsonObject projectObject = document.object().value(QStringLiteral("project")).toObject();
    return projectObject.value(QStringLiteral("canvas"))
        .toObject()
        .value(QStringLiteral("backgroundImagePath"))
        .toString();
}

QString storedPathForProject(const QString& projectPath, const QString& imagePath) {
    const QFileInfo imageInfo(imagePath);
    if (!imageInfo.exists()) {
        return {};
    }
    const QFileInfo projectInfo(projectPath);
    if (!projectInfo.exists()) {
        return imageInfo.absoluteFilePath();
    }
    const QString projectDir = projectInfo.absolutePath();
    if (imageInfo.absoluteFilePath().startsWith(projectDir + QDir::separator())) {
        return QDir(projectDir).relativeFilePath(imageInfo.absoluteFilePath());
    }
    return imageInfo.absoluteFilePath();
}

QString fallbackProjectDirectory() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (path.isEmpty()) {
        path = QDir::homePath();
    }
    return QDir::cleanPath(path);
}

QString projectParentDirectoryForPath(const QString& projectPath) {
    const QFileInfo projectFile(projectPath);
    const QFileInfo projectFolder(projectFile.absolutePath());
    return projectFolder.fileName().compare(projectFile.completeBaseName(),
                                            Qt::CaseInsensitive) == 0
               ? projectFolder.absolutePath()
               : projectFile.absolutePath();
}

QString standardProjectFolderForPath(const QString& projectPath) {
    const QFileInfo projectFile(projectPath);
    const QFileInfo folder(projectFile.absolutePath());
    if (folder.fileName().compare(projectFile.completeBaseName(), Qt::CaseInsensitive) == 0 &&
        QFileInfo(QDir(folder.absoluteFilePath()).filePath(projectFile.fileName())).exists()) {
        return folder.absoluteFilePath();
    }
    return {};
}

bool copyDirectoryRecursively(const QString& sourcePath, const QString& destinationPath) {
    QDir source(sourcePath);
    if (!source.exists()) {
        return true;
    }
    if (!QDir().mkpath(destinationPath)) {
        return false;
    }
    const QFileInfoList entries =
        source.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    for (const QFileInfo& entry : entries) {
        const QString destination = QDir(destinationPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), destination)) {
                return false;
            }
        } else if (QFile::exists(destination) && !QFile::remove(destination)) {
            return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}

bool deleteProjectFromDisk(const QString& projectPath) {
    const QString standardFolder = standardProjectFolderForPath(projectPath);
    if (!standardFolder.isEmpty()) {
        const QFileInfo folder(standardFolder);
        const QString absoluteFolder = folder.absoluteFilePath();
        if (absoluteFolder == QDir::rootPath() || absoluteFolder == QDir::homePath()) {
            return false;
        }
        return QDir(absoluteFolder).removeRecursively();
    }
    return QFile::remove(projectPath);
}

QRect cropSourceRect(const QRectF& normalized, const QSize& sourceSize) {
    const QRectF sourceRect(normalized.x() * sourceSize.width(),
                            normalized.y() * sourceSize.height(),
                            normalized.width() * sourceSize.width(),
                            normalized.height() * sourceSize.height());
    return sourceRect.toAlignedRect().intersected(QRect(QPoint(0, 0), sourceSize));
}
} // namespace

class RecentProjectsModel : public QAbstractListModel {
public:
    enum SortMode { LastEdited, Name, Created, Path };
    explicit RecentProjectsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setEntries(QVector<RecentProjectEntry> entries) {
        for (RecentProjectEntry& entry : entries) {
            if (entry.backgroundImagePath.isEmpty() && QFileInfo::exists(entry.filePath)) {
                entry.backgroundImagePath = backgroundPathFromProjectFile(entry.filePath);
            }
        }
        m_all = std::move(entries);
        rebuild();
    }

    void setFilter(const QString& filter) {
        m_filter = filter;
        rebuild();
    }

    void setSortMode(SortMode mode) {
        m_sortMode = mode;
        rebuild();
    }

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
            return {};
        }
        const RecentProjectEntry& entry = m_entries[index.row()];
        switch (role) {
        case Qt::DisplayRole:
            return entry.name;
        case FilePathRole:
            return entry.filePath;
        case UpdatedAtRole:
            return entry.updatedAt;
        case CreatedAtRole:
            return entry.createdAt;
        case RowCountRole:
            return entry.rowCount;
        case ImageCountRole:
            return entry.imageCount;
        case ExistsRole:
            return QFileInfo::exists(entry.filePath);
        case CoverPathRole:
            return resolveProjectRelativePath(entry.filePath, entry.thumbnailPath);
        case BackgroundPathRole:
            return resolveProjectRelativePath(entry.filePath, entry.backgroundImagePath);
        default:
            return {};
        }
    }

    RecentProjectEntry entryAt(const QModelIndex& index) const {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
            return {};
        }
        return m_entries[index.row()];
    }

private:
    void rebuild() {
        beginResetModel();
        m_entries.clear();
        for (const RecentProjectEntry& entry : m_all) {
            const QString term = m_filter.trimmed();
            if (term.isEmpty() || entry.name.contains(term, Qt::CaseInsensitive)) {
                m_entries.append(entry);
            }
        }
        std::sort(m_entries.begin(), m_entries.end(),
                  [this](const RecentProjectEntry& lhs, const RecentProjectEntry& rhs) {
                      switch (m_sortMode) {
                      case Name:
                          return lhs.name.localeAwareCompare(rhs.name) < 0;
                      case Created:
                          return lhs.createdAt > rhs.createdAt;
                      case Path:
                          return lhs.filePath.localeAwareCompare(rhs.filePath) < 0;
                      case LastEdited:
                      default:
                          return lhs.updatedAt > rhs.updatedAt;
                      }
                  });
        endResetModel();
    }

    QVector<RecentProjectEntry> m_all;
    QVector<RecentProjectEntry> m_entries;
    QString m_filter;
    SortMode m_sortMode{LastEdited};
};

class ProjectDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect r = option.rect.adjusted(8, 6, -8, -6);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const ThemeTokens& colors = activeThemeTokens();
        const QColor fill = selected
                                ? colors.selection
                                : (hovered ? colors.controlFillHovered : colors.elevatedBackground);
        const QColor border = selected ? colors.accent : colors.border;
        painter->setPen(QPen(border, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(r, 10, 10);

        const QRect thumb(r.left() + 12, r.top() + 12, 74, 54);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.controlFill);
        painter->drawRoundedRect(thumb, 8, 8);

        const QString coverPath = index.data(CoverPathRole).toString();
        const QString backgroundPath = index.data(BackgroundPathRole).toString();
        const QString displayImagePath = !coverPath.isEmpty() ? coverPath : backgroundPath;
        const QPixmap cover(displayImagePath);
        if (!cover.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(thumb, 8, 8);
            painter->setClipPath(clip);
            const QSize targetSize = thumb.size();
            const QSize sourceSize = cover.size();
            const qreal targetRatio =
                static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
            const qreal sourceRatio =
                static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
            QRect sourceRect;
            if (sourceRatio > targetRatio) {
                const int cropWidth = qRound(sourceSize.height() * targetRatio);
                sourceRect =
                    QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
            } else {
                const int cropHeight = qRound(sourceSize.width() / targetRatio);
                sourceRect = QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(),
                                   cropHeight);
            }
            painter->drawPixmap(thumb, cover, sourceRect);
            painter->setClipping(false);
        } else {
            QFont monogramFont = option.font;
            monogramFont.setBold(true);
            monogramFont.setPointSize(monogramFont.pointSize() + 5);
            painter->setFont(monogramFont);
            painter->setPen(colors.primaryText);
            painter->drawText(thumb, Qt::AlignCenter, QStringLiteral("TLM"));
        }

        const QString name = index.data(Qt::DisplayRole).toString();
        const QString path = index.data(FilePathRole).toString();
        const QDateTime updated = index.data(UpdatedAtRole).toDateTime();
        const QDateTime created = index.data(CreatedAtRole).toDateTime();
        const bool exists = index.data(ExistsRole).toBool();
        QRect textRect = r.adjusted(100, 10, -12, -8);
        QFont titleFont = option.font;
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 1);
        painter->setFont(titleFont);
        painter->setPen(colors.primaryText);
        painter->drawText(
            textRect, Qt::AlignLeft | Qt::AlignTop,
            painter->fontMetrics().elidedText(name, Qt::ElideRight, textRect.width()));
        painter->setFont(option.font);
        const QColor metaColor = exists ? colors.tertiaryText : colors.destructive;
        painter->setPen(metaColor);
        const QString meta =
            QCoreApplication::translate("tlm::ProjectsPage",
                                        "Updated %1  |  Created %2  |  %3 rows, %4 images%5")
                .arg(updated.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                     created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")),
                     index.data(RowCountRole).toString(), index.data(ImageCountRole).toString(),
                     exists ? QString()
                            : QCoreApplication::translate("tlm::ProjectsPage", "  |  Missing"));
        painter->drawText(
            textRect.adjusted(0, 24, 0, 0), Qt::AlignLeft | Qt::AlignTop,
            painter->fontMetrics().elidedText(meta, Qt::ElideRight, textRect.width()));
        painter->drawText(
            textRect.adjusted(0, 46, 0, 0), Qt::AlignLeft | Qt::AlignTop,
            painter->fontMetrics().elidedText(path, Qt::ElideMiddle, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(400, 92);
    }
};

ProjectsPage::ProjectsPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                           AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_recentProjects(recentProjects),
      m_settings(settings) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(12);

    auto* top = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Search"));
    m_search->addAction(vkui::icon(vkui::VkSymbol::Search, vkui::VkIconRole::Secondary),
                        QLineEdit::LeadingPosition);
    m_sort = new QComboBox(this);
    m_sort->addItems({tr("Last Edited"), tr("Name"), tr("Created"), tr("Path")});
    m_sort->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_sort->setMaximumWidth(260);
    vkui::setComboBoxElideMode(*m_sort, Qt::ElideRight);
    top->addWidget(m_search, 1);
    top->addWidget(m_sort);
    root->addLayout(top);

    m_view = new QListView(this);
    m_model = new RecentProjectsModel(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ProjectDelegate(m_view));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setStyleSheet(QStringLiteral("QListView{background:transparent;outline:0;}"));
    root->addWidget(m_view, 1);

    connect(m_recentProjects, &RecentProjectsStore::changed, this, &ProjectsPage::refresh);
    connect(m_search, &QLineEdit::textChanged, m_model, &RecentProjectsModel::setFilter);
    connect(m_sort, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_model->setSortMode(static_cast<RecentProjectsModel::SortMode>(index));
    });
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        const RecentProjectEntry entry = m_model->entryAt(index);
        if (QFileInfo::exists(entry.filePath)) {
            emit openProjectRequested(entry.filePath);
        }
    });
    connect(m_view, &QListView::customContextMenuRequested, this,
            &ProjectsPage::showProjectContextMenu);

    refresh();
}

void ProjectsPage::refresh() {
    m_model->setEntries(m_recentProjects->entries());
}

void ProjectsPage::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void ProjectsPage::openProjectFromDialog() {
    const QString directory =
        m_settings ? m_settings->defaultProjectDirectory() : fallbackProjectDirectory();
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Project"), directory,
                                     tr("TierListMaker Projects (*.tlmproject)"));
    if (!path.isEmpty()) {
        emit openProjectRequested(path);
    }
}

void ProjectsPage::retranslateUi() {
    if (m_search) {
        m_search->setPlaceholderText(tr("Search"));
    }
    if (m_sort) {
        const int current = m_sort->currentIndex();
        const QSignalBlocker blocker(m_sort);
        m_sort->clear();
        m_sort->addItems({tr("Last Edited"), tr("Name"), tr("Created"), tr("Path")});
        m_sort->setCurrentIndex(qBound(0, current, m_sort->count() - 1));
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

QString ProjectsPage::selectedPath() const {
    return m_view->currentIndex().data(FilePathRole).toString();
}

RecentProjectEntry ProjectsPage::selectedEntry() const {
    return m_model ? m_model->entryAt(m_view->currentIndex()) : RecentProjectEntry{};
}

void ProjectsPage::showProjectContextMenu(const QPoint& point) {
    const QModelIndex index = m_view->indexAt(point);
    if (!index.isValid()) {
        QMenu emptyMenu(this);
        QAction* openProjectAction =
            emptyMenu.addAction(vkui::icon(vkui::VkSymbol::Folder), tr("Open Project..."));
        if (emptyMenu.exec(m_view->viewport()->mapToGlobal(point)) == openProjectAction) {
            openProjectFromDialog();
        }
        return;
    }
    m_view->setCurrentIndex(index);
    const RecentProjectEntry entry = selectedEntry();
    const bool exists = QFileInfo::exists(entry.filePath);

    QMenu menu(this);
    QAction* openAction = menu.addAction(vkui::icon(vkui::VkSymbol::Folder), tr("Open"));
    QAction* saveAsAction = menu.addAction(vkui::icon(vkui::VkSymbol::Save),
                                           tr("Save As Project..."));
    QAction* renameAction = menu.addAction(vkui::icon(vkui::VkSymbol::Rename), tr("Rename"));
    QAction* coverAction = menu.addAction(vkui::icon(vkui::VkSymbol::Image), tr("Choose Cover"));
    QAction* revealAction = menu.addAction(vkui::icon(vkui::VkSymbol::Reveal), tr("Reveal"));
    menu.addSeparator();
    QAction* deleteAction =
        menu.addAction(vkui::icon(vkui::VkSymbol::Trash, vkui::VkIconRole::Destructive),
                       tr("Delete Project"));

    for (QAction* action : {openAction, saveAsAction, renameAction, coverAction, revealAction,
                            deleteAction}) {
        action->setEnabled(exists);
    }

    QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(point));
    if (chosen == openAction) {
        openSelectedProject();
    } else if (chosen == saveAsAction) {
        saveSelectedProjectAs();
    } else if (chosen == renameAction) {
        renameSelectedProject();
    } else if (chosen == coverAction) {
        chooseCoverForSelectedProject();
    } else if (chosen == revealAction) {
        revealSelectedProject();
    } else if (chosen == deleteAction) {
        deleteSelectedProject();
    }
}

void ProjectsPage::openSelectedProject() {
    const QString path = selectedPath();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        emit openProjectRequested(path);
    }
}

void ProjectsPage::renameSelectedProject() {
    const QString path = selectedPath();
    if (path.isEmpty() || !QFileInfo::exists(path) || !m_repository) {
        return;
    }

    auto projectResult = m_repository->openProject(path);
    if (!projectResult) {
        QMessageBox::warning(this, tr("Rename Project"), projectResult.error().message);
        return;
    }

    TierProject project = projectResult.takeValue();
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename Project"), tr("Project name:"), QLineEdit::Normal,
                              project.name, &accepted)
            .trimmed();
    if (!accepted || name.isEmpty() || name == project.name) {
        return;
    }

    project.name = name;
    project.touch();
    auto result = m_repository->saveProject(project, path);
    if (!result) {
        QMessageBox::warning(this, tr("Rename Project"), result.error().message);
        return;
    }
    m_recentProjects->addOrUpdate(project);
}

void ProjectsPage::chooseCoverForSelectedProject() {
    const QString projectPath = selectedPath();
    if (projectPath.isEmpty() || !QFileInfo::exists(projectPath) || !m_repository) {
        return;
    }

    auto projectResult = m_repository->openProject(projectPath);
    if (!projectResult) {
        QMessageBox::warning(
            this, tr("Cover Image"),
            projectResult.error().details.isEmpty()
                ? projectResult.error().message
                : QStringLiteral("%1\n\n%2")
                      .arg(projectResult.error().message, projectResult.error().details));
        return;
    }

    QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All Files (*)");
    const QString imagePath = QFileDialog::getOpenFileName(
        this, tr("Choose Project Cover"), QFileInfo(projectPath).absolutePath(), filter);
    if (imagePath.isEmpty()) {
        return;
    }

    TierProject project = projectResult.takeValue();
    QImage image(imagePath);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Cover Image"), tr("Could not read the selected image."));
        return;
    }

    TierImage coverImage;
    const QFileInfo imageInfo(imagePath);
    coverImage.sourcePath = imageInfo.absoluteFilePath();
    coverImage.originalFileName = imageInfo.fileName();
    coverImage.displayName = imageInfo.completeBaseName();
    coverImage.width = image.width();
    coverImage.height = image.height();

    QPixmap pixmap = QPixmap::fromImage(image);
    ImageEditDialog dialog(coverImage, pixmap, this, QSizeF(74.0, 54.0));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString assetDirectory = QDir(QFileInfo(projectPath).absolutePath()).filePath(
        QStringLiteral("assets"));
    if (!QDir().mkpath(assetDirectory)) {
        QMessageBox::warning(this, tr("Cover Image"),
                             tr("Could not create the project assets folder."));
        return;
    }

    const QRect sourceRect = cropSourceRect(dialog.cropRect(), image.size());
    const QImage cropped = image.copy(sourceRect.isValid() ? sourceRect : image.rect());
    const QString coverPath =
        QDir(assetDirectory)
            .filePath(QStringLiteral("cover-%1.png").arg(QUuid::createUuid().toString(
                QUuid::WithoutBraces)));
    if (!cropped.save(coverPath, "PNG")) {
        QMessageBox::warning(this, tr("Cover Image"), tr("Could not save the cover image."));
        return;
    }

    project.thumbnailPath = storedPathForProject(projectPath, coverPath);
    project.updatedAt = QDateTime::currentDateTimeUtc();
    project.dirty = true;
    auto saveResult = m_repository->saveProject(project, projectPath);
    if (!saveResult) {
        QMessageBox::warning(
            this, tr("Cover Image"),
            saveResult.error().details.isEmpty()
                ? saveResult.error().message
                : QStringLiteral("%1\n\n%2")
                      .arg(saveResult.error().message, saveResult.error().details));
        return;
    }

    m_recentProjects->addOrUpdate(project);
    Logger::info(QStringLiteral("projects.cover.choose project=\"%1\" image=\"%2\"")
                     .arg(projectPath, project.thumbnailPath));
}

void ProjectsPage::revealSelectedProject() {
    const QString path = selectedPath();
    if (!path.isEmpty()) {
        platform::revealInFileManager(path);
    }
}

void ProjectsPage::saveSelectedProjectAs() {
    const QString sourcePath = selectedPath();
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath) || !m_repository) {
        return;
    }

    auto projectResult = m_repository->openProject(sourcePath);
    if (!projectResult) {
        QMessageBox::warning(this, tr("Save As Project"), projectResult.error().message);
        return;
    }
    TierProject project = projectResult.takeValue();

    const QString defaultDirectory =
        m_settings ? m_settings->defaultProjectDirectory() : fallbackProjectDirectory();
    ProjectLocationDialog dialog(project.name, projectParentDirectoryForPath(sourcePath),
                                 defaultDirectory, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString targetPath = QFileInfo(dialog.projectFilePath()).absoluteFilePath();
    const QString sourceAbsolute = QFileInfo(sourcePath).absoluteFilePath();
    if (targetPath == sourceAbsolute) {
        return;
    }
    if (QFileInfo::exists(targetPath)) {
        QMessageBox::warning(this, tr("Save As Project"),
                             tr("A project with this name already exists."));
        return;
    }
    const QString sourceRoot = standardProjectFolderForPath(sourcePath);
    if (!sourceRoot.isEmpty() &&
        QDir::cleanPath(targetPath).startsWith(QDir::cleanPath(sourceRoot) + QLatin1Char('/'),
                                               Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Save As Project"),
                             tr("Choose a location outside the current project folder."));
        return;
    }

    const QString targetDir = QFileInfo(targetPath).absolutePath();
    if (!QDir().mkpath(targetDir)) {
        QMessageBox::warning(this, tr("Save As Project"),
                             tr("Could not create the project folder."));
        return;
    }

    const QString sourceAssets =
        QDir(QFileInfo(sourcePath).absolutePath()).filePath(QStringLiteral("assets"));
    const QString targetAssets = QDir(targetDir).filePath(QStringLiteral("assets"));
    if (!copyDirectoryRecursively(sourceAssets, targetAssets)) {
        QMessageBox::warning(this, tr("Save As Project"),
                             tr("Could not copy the project assets."));
        return;
    }

    project.name = dialog.projectName();
    project.filePath = targetPath;
    project.touch();
    auto saveResult = m_repository->saveProject(project, targetPath);
    if (!saveResult) {
        QMessageBox::warning(this, tr("Save As Project"), saveResult.error().message);
        return;
    }

    if (!deleteProjectFromDisk(sourcePath)) {
        QMessageBox::warning(this, tr("Save As Project"),
                             tr("The project was saved, but the old project could not be removed."));
    }
    m_recentProjects->remove(sourcePath);
    m_recentProjects->addOrUpdate(project);
    if (m_settings && dialog.shouldUseAsDefaultDirectory()) {
        m_settings->setDefaultProjectDirectory(dialog.parentDirectory());
    }
    Logger::info(QStringLiteral("projects.save_as.move source=\"%1\" target=\"%2\"")
                     .arg(sourceAbsolute, targetPath));
}

void ProjectsPage::deleteSelectedProject() {
    const RecentProjectEntry entry = selectedEntry();
    if (entry.filePath.isEmpty() || !QFileInfo::exists(entry.filePath)) {
        return;
    }

    const int choice = QMessageBox::warning(
        this, tr("Delete Project"),
        tr("Delete \"%1\" and its project files? This cannot be undone.").arg(entry.name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes) {
        return;
    }

    if (!deleteProjectFromDisk(entry.filePath)) {
        QMessageBox::warning(this, tr("Delete Project"),
                             tr("Could not delete the project from disk."));
        return;
    }
    m_recentProjects->remove(entry.filePath);
    Logger::info(QStringLiteral("projects.delete path=\"%1\"").arg(entry.filePath));
}

} // namespace tlm
