#include "pages/ProjectsPage.h"

#include "logging/Logger.h"
#include "pages/ProjectLocationDialog.h"
#include "persistence/ProjectRepository.h"
#include "platform/Platform.h"
#include "settings/AppSettings.h"
#include "theme/Theme.h"
#include "tier/CropEditorWidget.h"
#include "window/AppDialog.h"

#include <QAbstractItemView>
#include <QAbstractListModel>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

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

QJsonObject cropRectToJson(const QRectF& rect) {
    return QJsonObject{{QStringLiteral("x"), rect.x()},
                       {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("width"), rect.width()},
                       {QStringLiteral("height"), rect.height()}};
}

QRectF cropRectFromJson(const QJsonObject& object) {
    const QRectF rect(object.value(QStringLiteral("x")).toDouble(),
                      object.value(QStringLiteral("y")).toDouble(),
                      object.value(QStringLiteral("width")).toDouble(),
                      object.value(QStringLiteral("height")).toDouble());
    return rect.isValid() ? rect : QRectF();
}

QString uniqueCoverAssetPath(const QString& projectPath, const QString& prefix,
                             const QString& suffix) {
    const QString assetDirectory =
        QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral("assets"));
    const QString extension = suffix.isEmpty() ? QStringLiteral("png") : suffix.toLower();
    return QDir(assetDirectory)
        .filePath(QStringLiteral("%1-%2.%3")
                      .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces), extension));
}

QString copyCoverSourceToAssets(const QString& projectPath, const QString& sourcePath) {
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return {};
    }
    const QString destination =
        uniqueCoverAssetPath(projectPath, QStringLiteral("cover-source"), sourceInfo.suffix());
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        return {};
    }
    if (!QFile::copy(sourceInfo.absoluteFilePath(), destination)) {
        return {};
    }
    return destination;
}

QString coverSourcePath(const TierProject& project) {
    return project.cover.value(QStringLiteral("sourceImagePath")).toString();
}

QString coverCroppedPath(const TierProject& project) {
    const QString cropped = project.cover.value(QStringLiteral("croppedImagePath")).toString();
    return cropped.isEmpty() ? project.thumbnailPath : cropped;
}

void removeManagedCoverAsset(const QString& projectPath, const QString& storedPath) {
    if (storedPath.isEmpty()) {
        return;
    }
    const QString candidate = QDir::cleanPath(
        QFileInfo(resolveProjectRelativePath(projectPath, storedPath)).absoluteFilePath());
    const QString assetsPrefix =
        QDir::cleanPath(
            QFileInfo(
                QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral("assets")))
                .absoluteFilePath()) +
        QDir::separator();
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    if (candidate.startsWith(assetsPrefix, pathCase)) {
        QFile::remove(candidate);
    }
}

QString fallbackProjectDirectory() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (path.isEmpty()) {
        path = QDir::homePath();
    }
    return QDir::cleanPath(QDir(path).filePath(QStringLiteral("TierListMaker")));
}

QString projectParentDirectoryForPath(const QString& projectPath) {
    const QFileInfo projectFile(projectPath);
    const QFileInfo projectFolder(projectFile.absolutePath());
    return projectFolder.fileName().compare(projectFile.completeBaseName(), Qt::CaseInsensitive) ==
                   0
               ? projectFolder.absolutePath()
               : projectFile.absolutePath();
}

QString projectFilePathForName(const QString& parentDirectory, const QString& name) {
    TierProject candidate = TierProject::createUntitled();
    candidate.name = name.trimmed();
    const QString fileName = candidate.suggestedFileName();
    const QString folderName = QFileInfo(fileName).completeBaseName();
    return QDir(QDir(parentDirectory).filePath(folderName)).filePath(fileName);
}

bool sameFilePath(const QString& left, const QString& right) {
#if defined(Q_OS_WIN)
    return QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(),
                                                      Qt::CaseInsensitive) == 0;
#else
    return QFileInfo(left).absoluteFilePath() == QFileInfo(right).absoluteFilePath();
#endif
}

bool indexIsUnderCursor(const QStyleOptionViewItem& option, const QModelIndex& index) {
    const auto* viewport = qobject_cast<const QWidget*>(option.widget);
    const auto* view =
        qobject_cast<const QAbstractItemView*>(viewport ? viewport->parentWidget() : nullptr);
    if (!viewport || !view) {
        return option.state.testFlag(QStyle::State_MouseOver);
    }
    const QPoint localPos = viewport->mapFromGlobal(QCursor::pos());
    return viewport->rect().contains(localPos) && view->indexAt(localPos) == index;
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
    const QRectF sourceRect(
        normalized.x() * sourceSize.width(), normalized.y() * sourceSize.height(),
        normalized.width() * sourceSize.width(), normalized.height() * sourceSize.height());
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
        const bool hovered =
            option.state.testFlag(QStyle::State_MouseOver) && indexIsUnderCursor(option, index);
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

class ProjectCoverDialog final : public AppDialog {
    Q_DECLARE_TR_FUNCTIONS(tlm::ProjectCoverDialog)

public:
    ProjectCoverDialog(const QString& initialImagePath, const QRectF& initialCrop,
                       const QString& startDirectory, bool hasCustomCover,
                       QWidget* parent = nullptr)
        : AppDialog(tr("Project Cover"), parent), m_startDirectory(startDirectory),
          m_editorHost(new QWidget(this)), m_editorLayout(new QVBoxLayout(m_editorHost)),
          m_placeholder(new QLabel(tr("Choose an image to use as the project cover."), this)) {
        setObjectName(QStringLiteral("ProjectCoverDialog"));
        setMinimumWidth(520);
        resize(560, 560);
        contentLayout()->setSpacing(14);

        auto* title = new QLabel(tr("Project cover crop"), this);
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 4);
        titleFont.setBold(true);
        title->setFont(titleFont);
        contentLayout()->addWidget(title);

        m_editorLayout->setContentsMargins(0, 0, 0, 0);
        m_editorLayout->setSpacing(0);
        m_placeholder->setAlignment(Qt::AlignCenter);
        m_placeholder->setMinimumHeight(360);
        m_placeholder->setWordWrap(true);
        m_placeholder->setStyleSheet(
            QStringLiteral("QLabel{border:1px solid palette(mid);border-radius:10px;"
                           "background:palette(alternate-base);color:palette(mid);}"));
        m_editorLayout->addWidget(m_placeholder);
        contentLayout()->addWidget(m_editorHost, 1);

        auto* actions = new QHBoxLayout;
        m_chooseButton =
            new QPushButton(vkui::icon(vkui::VkSymbol::Image), tr("Choose Image"), this);
        actions->addWidget(m_chooseButton);
        m_clearButton =
            new QPushButton(vkui::icon(vkui::VkSymbol::Clear), tr("Clear Custom Cover"), this);
        m_clearButton->setVisible(hasCustomCover);
        actions->addWidget(m_clearButton);
        actions->addStretch(1);
        contentLayout()->addLayout(actions);

        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
        contentLayout()->addWidget(buttons);
        connect(m_chooseButton, &QPushButton::clicked, this, &ProjectCoverDialog::chooseImage);
        connect(m_clearButton, &QPushButton::clicked, this, &ProjectCoverDialog::stageClearCover);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_clearCover) {
                accept();
                return;
            }
            if (m_image.isNull() || !m_cropEditor) {
                QMessageBox::warning(this, tr("Project Cover"), tr("Choose a cover image."));
                return;
            }
            accept();
        });

        if (!initialImagePath.isEmpty()) {
            loadImage(initialImagePath, initialCrop, false);
        }
    }

    QString sourceImagePath() const {
        return m_sourceImagePath;
    }

    QImage image() const {
        return m_image;
    }

    QRectF cropRect() const {
        return m_cropEditor ? m_cropEditor->cropRect() : QRectF();
    }

    bool shouldClearCover() const {
        return m_clearCover;
    }

private:
    void chooseImage() {
        const QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All Files (*)");
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Project Cover"),
                                                          m_startDirectory, filter);
        if (path.isEmpty()) {
            return;
        }
        loadImage(path, {}, true);
    }

    void loadImage(const QString& path, const QRectF& initialCrop, bool warnOnFailure) {
        QImage image(path);
        if (image.isNull()) {
            if (warnOnFailure) {
                QMessageBox::warning(this, tr("Project Cover"),
                                     tr("Could not read the selected image."));
            }
            return;
        }

        m_startDirectory = QFileInfo(path).absolutePath();
        m_sourceImagePath = QFileInfo(path).absoluteFilePath();
        m_clearCover = false;
        m_image = image;
        const QPixmap pixmap = QPixmap::fromImage(m_image);
        if (!m_cropEditor) {
            m_cropEditor =
                new CropEditorWidget(pixmap, initialCrop, QSizeF(74.0, 54.0), m_editorHost);
            m_editorLayout->addWidget(m_cropEditor);
        } else {
            m_cropEditor->setPixmap(pixmap, initialCrop);
        }
        m_placeholder->hide();
        m_cropEditor->show();
        m_chooseButton->setText(tr("Change Image"));
        m_clearButton->setVisible(true);
        m_clearButton->setEnabled(true);
    }

    void stageClearCover() {
        m_clearCover = true;
        m_sourceImagePath.clear();
        m_image = {};
        if (m_cropEditor) {
            m_cropEditor->hide();
        }
        m_placeholder->setText(tr("Custom cover will be removed when you save."));
        m_placeholder->show();
        m_chooseButton->setText(tr("Choose Image"));
        m_clearButton->setEnabled(false);
    }

    QString m_startDirectory;
    QString m_sourceImagePath;
    QImage m_image;
    QWidget* m_editorHost{nullptr};
    QVBoxLayout* m_editorLayout{nullptr};
    QLabel* m_placeholder{nullptr};
    CropEditorWidget* m_cropEditor{nullptr};
    QPushButton* m_chooseButton{nullptr};
    QPushButton* m_clearButton{nullptr};
    bool m_clearCover{false};
};

ProjectsPage::ProjectsPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
                           AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_recentProjects(recentProjects),
      m_settings(settings) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(12);

    auto* top = new QHBoxLayout;
    m_openProjectButton = new QToolButton(this);
    m_openProjectButton->setObjectName(QStringLiteral("OpenProjectButton"));
    m_openProjectButton->setToolTip(tr("Open Project"));
    m_openProjectButton->setIcon(vkui::icon(vkui::VkSymbol::Folder));
    m_openProjectButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_openProjectButton->setCursor(Qt::PointingHandCursor);
    m_openProjectButton->setFocusPolicy(Qt::NoFocus);
    m_openProjectButton->setFixedSize(34, 34);
    m_openProjectButton->setIconSize(QSize(19, 19));
    m_openProjectButton->setAutoRaise(true);
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
    top->addWidget(m_openProjectButton);
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
    m_view->setMouseTracking(true);
    m_view->viewport()->setMouseTracking(true);
    m_view->setStyleSheet(QStringLiteral("QListView{background:transparent;outline:0;}"));
    root->addWidget(m_view, 1);

    connect(m_recentProjects, &RecentProjectsStore::changed, this, &ProjectsPage::refresh);
    connect(m_openProjectButton, &QToolButton::clicked, this, &ProjectsPage::openProjectFromDialog);
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
    connect(m_view, &QListView::pressed, this,
            [this](const QModelIndex&) { m_view->viewport()->update(); });
    connect(m_view, &QListView::entered, this,
            [this](const QModelIndex&) { m_view->viewport()->update(); });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { m_view->viewport()->update(); });
    connect(m_view, &QListView::customContextMenuRequested, this,
            &ProjectsPage::showProjectContextMenu);

    refresh();
}

void ProjectsPage::refresh() {
    m_model->setEntries(m_recentProjects->entries());
}

void ProjectsPage::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->setCursorPosition(m_search->text().size());
}

void ProjectsPage::openProjectFromDialog() {
    const QString directory =
        m_settings ? m_settings->defaultProjectDirectory() : fallbackProjectDirectory();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), directory,
                                                      tr("TierListMaker Projects (*.tlmproject)"));
    if (!path.isEmpty()) {
        emit openProjectRequested(path);
    }
}

void ProjectsPage::retranslateUi() {
    if (m_openProjectButton) {
        m_openProjectButton->setToolTip(tr("Open Project"));
    }
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
        return;
    }
    m_view->setCurrentIndex(index);
    const RecentProjectEntry entry = selectedEntry();
    const bool exists = QFileInfo::exists(entry.filePath);

    QMenu menu(this);
    QAction* openAction = menu.addAction(vkui::icon(vkui::VkSymbol::Folder), tr("Open"));
    QAction* saveAsAction =
        menu.addAction(vkui::icon(vkui::VkSymbol::Save), tr("Save As Project..."));
    QAction* renameAction = menu.addAction(vkui::icon(vkui::VkSymbol::Rename), tr("Rename"));
    QAction* coverAction = menu.addAction(vkui::icon(vkui::VkSymbol::Image), tr("Choose Cover"));
    QAction* revealAction = menu.addAction(vkui::icon(vkui::VkSymbol::Reveal), tr("Reveal"));
    QAction* removeRecordAction =
        menu.addAction(vkui::icon(vkui::VkSymbol::Remove), tr("Remove from Recent Projects"));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(
        vkui::icon(vkui::VkSymbol::Trash, vkui::VkIconRole::Destructive), tr("Delete Project"));

    for (QAction* action :
         {openAction, saveAsAction, renameAction, coverAction, revealAction, deleteAction}) {
        action->setEnabled(exists);
    }

    QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(point));
    m_view->viewport()->update();
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
    } else if (chosen == removeRecordAction) {
        removeSelectedRecord();
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
    const QString sourcePath = selectedPath();
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath) || !m_repository) {
        return;
    }

    auto projectResult = m_repository->openProject(sourcePath);
    if (!projectResult) {
        QMessageBox::warning(this, tr("Rename Project"), projectResult.error().message);
        return;
    }

    TierProject project = projectResult.takeValue();
    AppDialog dialog(tr("Rename Project"), this);
    dialog.setMinimumWidth(420);
    auto* nameEdit = new QLineEdit(project.name, &dialog);
    nameEdit->setClearButtonEnabled(true);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("Project name"), nameEdit);
    dialog.contentLayout()->addLayout(form);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
    dialog.contentLayout()->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, tr("Rename Project"), tr("Enter a project name."));
            return;
        }
        dialog.accept();
    });
    QTimer::singleShot(0, &dialog, [&]() {
        nameEdit->setFocus(Qt::OtherFocusReason);
        nameEdit->setCursorPosition(nameEdit->text().size());
    });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = nameEdit->text().trimmed();
    if (name == project.name) {
        return;
    }

    const QString sourceAbsolute = QFileInfo(sourcePath).absoluteFilePath();
    const QString parentDirectory = projectParentDirectoryForPath(sourcePath);
    const QString targetPath =
        QFileInfo(projectFilePathForName(parentDirectory, name)).absoluteFilePath();
    const QString targetDir = QFileInfo(targetPath).absolutePath();
    const bool sameProjectFile = sameFilePath(sourceAbsolute, targetPath);
    if (!sameProjectFile && QFileInfo::exists(targetPath)) {
        QMessageBox::warning(this, tr("Rename Project"),
                             tr("A project with this name already exists."));
        return;
    }

    QString activeSourceProjectFile = sourceAbsolute;
    const QString sourceRoot = standardProjectFolderForPath(sourcePath);
    if (!sourceRoot.isEmpty()) {
        const bool sameProjectFolder = sameFilePath(sourceRoot, targetDir);
        if (!sameProjectFolder) {
            if (QFileInfo::exists(targetDir)) {
                QMessageBox::warning(this, tr("Rename Project"),
                                     tr("A project folder with this name already exists."));
                return;
            }
            const QString targetParent = QFileInfo(targetDir).absolutePath();
            if (!QDir().mkpath(targetParent) || !QDir().rename(sourceRoot, targetDir)) {
                QMessageBox::warning(this, tr("Rename Project"),
                                     tr("Could not rename the project folder."));
                return;
            }
            activeSourceProjectFile = QDir(targetDir).filePath(QFileInfo(sourcePath).fileName());
        }
    } else if (!sameProjectFile) {
        if (!QDir().mkpath(targetDir)) {
            QMessageBox::warning(this, tr("Rename Project"),
                                 tr("Could not create the project folder."));
            return;
        }
        const QString sourceAssets =
            QDir(QFileInfo(sourcePath).absolutePath()).filePath(QStringLiteral("assets"));
        const QString targetAssets = QDir(targetDir).filePath(QStringLiteral("assets"));
        if (!copyDirectoryRecursively(sourceAssets, targetAssets)) {
            QMessageBox::warning(this, tr("Rename Project"),
                                 tr("Could not copy the project assets."));
            return;
        }
    }

    project.name = name;
    project.filePath = targetPath;
    project.touch();
    auto result = m_repository->saveProject(project, targetPath);
    if (!result) {
        QMessageBox::warning(this, tr("Rename Project"), result.error().message);
        return;
    }

    const bool removeOldProjectFile = !sameFilePath(activeSourceProjectFile, targetPath) &&
                                      QFileInfo::exists(activeSourceProjectFile);
    if (removeOldProjectFile && !QFile::remove(activeSourceProjectFile)) {
        QMessageBox::warning(this, tr("Rename Project"),
                             tr("The project was renamed, but the old project file could not be "
                                "removed."));
    }
    m_recentProjects->remove(sourcePath);
    m_recentProjects->addOrUpdate(project);
    Logger::info(QStringLiteral("projects.rename source=\"%1\" target=\"%2\"")
                     .arg(sourceAbsolute, targetPath));
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

    TierProject project = projectResult.takeValue();
    const QString storedSource = coverSourcePath(project);
    const QString storedCropped = coverCroppedPath(project);
    const QString currentSourcePath = resolveProjectRelativePath(
        projectPath, storedSource.isEmpty() ? storedCropped : storedSource);
    const QRectF currentCrop =
        cropRectFromJson(project.cover.value(QStringLiteral("crop")).toObject());
    const bool hasCustomCover = !storedSource.isEmpty() || !storedCropped.isEmpty();
    ProjectCoverDialog dialog(currentSourcePath, currentCrop, QFileInfo(projectPath).absolutePath(),
                              hasCustomCover, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString assetDirectory =
        QDir(QFileInfo(projectPath).absolutePath()).filePath(QStringLiteral("assets"));
    if (!QDir().mkpath(assetDirectory)) {
        QMessageBox::warning(this, tr("Cover Image"),
                             tr("Could not create the project assets folder."));
        return;
    }

    if (dialog.shouldClearCover()) {
        project.cover = {};
        project.thumbnailPath.clear();
        project.updatedAt = QDateTime::currentDateTimeUtc();
        project.dirty = true;
        auto saveResult = m_repository->saveProject(project, projectPath);
        if (!saveResult) {
            QMessageBox::warning(this, tr("Cover Image"), saveResult.error().message);
            return;
        }
        removeManagedCoverAsset(projectPath, storedSource);
        if (storedCropped != storedSource) {
            removeManagedCoverAsset(projectPath, storedCropped);
        }
        m_recentProjects->addOrUpdate(project);
        Logger::info(QStringLiteral("projects.cover.clear project=\"%1\"").arg(projectPath));
        return;
    }

    const QImage image = dialog.image();
    const QString selectedSource = dialog.sourceImagePath();
    QString projectSourcePath = selectedSource;
    bool copiedSource = false;
    const QString projectDirectory = QFileInfo(projectPath).absolutePath();
    const QString relativeSource =
        QDir(projectDirectory).relativeFilePath(QFileInfo(selectedSource).absoluteFilePath());
    const bool sourceAlreadyInProject = !QDir::isAbsolutePath(relativeSource) &&
                                        relativeSource != QStringLiteral("..") &&
                                        !relativeSource.startsWith(QStringLiteral("../")) &&
                                        !relativeSource.startsWith(QStringLiteral("..\\"));
    if (!sourceAlreadyInProject) {
        projectSourcePath = copyCoverSourceToAssets(projectPath, selectedSource);
        if (projectSourcePath.isEmpty()) {
            QMessageBox::warning(this, tr("Cover Image"),
                                 tr("Could not save the cover source image."));
            return;
        }
        copiedSource = true;
    }

    const QRect sourceRect = cropSourceRect(dialog.cropRect(), image.size());
    const QImage cropped = image.copy(sourceRect.isValid() ? sourceRect : image.rect());
    const QString coverPath =
        uniqueCoverAssetPath(projectPath, QStringLiteral("cover-crop"), QStringLiteral("png"));
    if (!cropped.save(coverPath, "PNG")) {
        if (copiedSource) {
            removeManagedCoverAsset(projectPath, projectSourcePath);
        }
        QMessageBox::warning(this, tr("Cover Image"), tr("Could not save the cover image."));
        return;
    }

    const QString storedSourcePath = storedPathForProject(projectPath, projectSourcePath);
    const QString storedCroppedPath = storedPathForProject(projectPath, coverPath);
    project.thumbnailPath = storedCroppedPath;
    project.cover = QJsonObject{{QStringLiteral("sourceImagePath"), storedSourcePath},
                                {QStringLiteral("croppedImagePath"), storedCroppedPath},
                                {QStringLiteral("crop"), cropRectToJson(dialog.cropRect())}};
    project.updatedAt = QDateTime::currentDateTimeUtc();
    project.dirty = true;
    auto saveResult = m_repository->saveProject(project, projectPath);
    if (!saveResult) {
        if (copiedSource) {
            removeManagedCoverAsset(projectPath, projectSourcePath);
        }
        removeManagedCoverAsset(projectPath, coverPath);
        QMessageBox::warning(
            this, tr("Cover Image"),
            saveResult.error().details.isEmpty()
                ? saveResult.error().message
                : QStringLiteral("%1\n\n%2")
                      .arg(saveResult.error().message, saveResult.error().details));
        return;
    }

    if (storedSource != storedSourcePath && storedSource != storedCroppedPath) {
        removeManagedCoverAsset(projectPath, storedSource);
    }
    if (storedCropped != storedCroppedPath && storedCropped != storedSourcePath &&
        storedCropped != storedSource) {
        removeManagedCoverAsset(projectPath, storedCropped);
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

void ProjectsPage::removeSelectedRecord() {
    const RecentProjectEntry entry = selectedEntry();
    if (entry.filePath.isEmpty() || !m_recentProjects) {
        return;
    }
    m_recentProjects->remove(entry.filePath);
    Logger::info(QStringLiteral("projects.recent.remove path=\"%1\"").arg(entry.filePath));
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
        QDir::cleanPath(targetPath)
            .startsWith(QDir::cleanPath(sourceRoot) + QLatin1Char('/'), Qt::CaseInsensitive)) {
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
        QMessageBox::warning(this, tr("Save As Project"), tr("Could not copy the project assets."));
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
        QMessageBox::warning(
            this, tr("Save As Project"),
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
    emit projectDeleted(QFileInfo(entry.filePath).absoluteFilePath());
    Logger::info(QStringLiteral("projects.delete path=\"%1\"").arg(entry.filePath));
}

} // namespace tlm
