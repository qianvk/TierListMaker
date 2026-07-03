#include "pages/ProjectsPage.h"

#include "logging/Logger.h"
#include "platform/Platform.h"
#include "persistence/ProjectRepository.h"
#include "widgets/RoundedButton.h"
#include "widgets/SearchField.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

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
    return projectObject.value(QStringLiteral("canvas")).toObject()
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
            const QString haystack = entry.name + QStringLiteral("\n") + entry.filePath;
            if (m_filter.trimmed().isEmpty() ||
                haystack.contains(m_filter, Qt::CaseInsensitive)) {
                m_entries.append(entry);
            }
        }
        std::sort(m_entries.begin(), m_entries.end(), [this](const RecentProjectEntry& lhs,
                                                             const RecentProjectEntry& rhs) {
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
        QColor fill = selected ? option.palette.highlight().color()
                               : option.palette.color(hovered ? QPalette::Midlight : QPalette::AlternateBase);
        fill.setAlpha(selected ? 90 : (hovered ? 210 : 188));
        QColor border = selected ? option.palette.highlight().color() : option.palette.color(QPalette::Mid);
        border.setAlpha(selected ? 150 : 90);
        painter->setPen(QPen(border, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(r, 10, 10);

        const QRect thumb(r.left() + 12, r.top() + 12, 74, 54);
        painter->setPen(Qt::NoPen);
        painter->setBrush(option.palette.color(QPalette::AlternateBase));
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
            const qreal targetRatio = static_cast<qreal>(targetSize.width()) / qMax(1, targetSize.height());
            const qreal sourceRatio = static_cast<qreal>(sourceSize.width()) / qMax(1, sourceSize.height());
            QRect sourceRect;
            if (sourceRatio > targetRatio) {
                const int cropWidth = qRound(sourceSize.height() * targetRatio);
                sourceRect = QRect((sourceSize.width() - cropWidth) / 2, 0, cropWidth, sourceSize.height());
            } else {
                const int cropHeight = qRound(sourceSize.width() / targetRatio);
                sourceRect = QRect(0, (sourceSize.height() - cropHeight) / 2, sourceSize.width(), cropHeight);
            }
            painter->drawPixmap(thumb, cover, sourceRect);
            painter->setClipping(false);
        } else {
            QFont monogramFont = option.font;
            monogramFont.setBold(true);
            monogramFont.setPointSize(monogramFont.pointSize() + 5);
            painter->setFont(monogramFont);
            painter->setPen(option.palette.color(QPalette::WindowText));
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
        painter->setPen(option.palette.color(QPalette::WindowText));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                          painter->fontMetrics().elidedText(name, Qt::ElideRight, textRect.width()));
        painter->setFont(option.font);
        painter->setPen(exists ? option.palette.color(QPalette::PlaceholderText) : QColor(QStringLiteral("#f7768e")));
        const QString meta =
            QCoreApplication::translate("tlm::ProjectsPage",
                                        "Updated %1  |  Created %2  |  %3 rows, %4 images%5")
                .arg(updated.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                     created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")),
                     index.data(RowCountRole).toString(), index.data(ImageCountRole).toString(),
                     exists ? QString()
                            : QCoreApplication::translate("tlm::ProjectsPage", "  |  Missing"));
        painter->drawText(textRect.adjusted(0, 24, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                          painter->fontMetrics().elidedText(meta, Qt::ElideRight, textRect.width()));
        painter->drawText(textRect.adjusted(0, 46, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                          painter->fontMetrics().elidedText(path, Qt::ElideMiddle, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(400, 92);
    }
};

ProjectsPage::ProjectsPage(ProjectRepository* repository, RecentProjectsStore* recentProjects, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_recentProjects(recentProjects) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(12);

    auto* top = new QHBoxLayout;
    m_search = new SearchField(this);
    m_sort = new QComboBox(this);
    m_sort->addItems({tr("Last Edited"), tr("Name"), tr("Created"), tr("Path")});
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
    m_view->setStyleSheet(QStringLiteral("QListView{background:transparent;outline:0;}"));
    root->addWidget(m_view, 1);

    auto* actions = new QHBoxLayout;
    m_openButton = new RoundedButton(tr("Open"), this);
    m_renameButton = new RoundedButton(tr("Rename"), this);
    m_coverButton = new RoundedButton(tr("Choose Cover"), this);
    m_revealButton = new RoundedButton(tr("Reveal"), this);
    m_duplicateButton = new RoundedButton(tr("Duplicate"), this);
    m_removeButton = new RoundedButton(tr("Remove"), this);
    m_deleteFileButton = new RoundedButton(tr("Delete File"), this);
    for (auto* button : {m_openButton, m_renameButton, m_coverButton, m_revealButton,
                         m_duplicateButton, m_removeButton, m_deleteFileButton}) {
        actions->addWidget(button);
    }
    actions->addStretch();
    root->addLayout(actions);

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
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            emit openProjectRequested(path);
        }
    });
    connect(m_renameButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (path.isEmpty()) {
            return;
        }
        const QString name = QInputDialog::getText(this, tr("Rename Recent Project"), tr("Display name:"));
        if (!name.trimmed().isEmpty()) {
            m_recentProjects->renameDisplayName(path, name.trimmed());
        }
    });
    connect(m_coverButton, &QPushButton::clicked, this, &ProjectsPage::chooseCoverForSelectedProject);
    connect(m_revealButton, &QPushButton::clicked, this,
            [this]() { platform::revealInFileManager(selectedPath()); });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (!path.isEmpty()) {
            m_recentProjects->remove(path);
        }
    });
    connect(m_duplicateButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return;
        }
        const QFileInfo info(path);
        const QString destination = QFileDialog::getSaveFileName(
            this, tr("Duplicate Project"),
            info.dir().filePath(info.completeBaseName() + QStringLiteral(" Copy.tlmproject")),
            tr("TierListMaker Projects (*.tlmproject)"));
        if (!destination.isEmpty() && QFile::copy(path, destination)) {
            emit openProjectRequested(destination);
        }
    });
    connect(m_deleteFileButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedPath();
        if (path.isEmpty()) {
            return;
        }
        if (QMessageBox::warning(this, tr("Delete Project File"),
                                 tr("Delete this project file from disk? This cannot be undone."),
                                 QMessageBox::Yes | QMessageBox::Cancel,
                                 QMessageBox::Cancel) == QMessageBox::Yes) {
            QFile::remove(path);
            m_recentProjects->remove(path);
        }
    });

    refresh();
}

void ProjectsPage::refresh() {
    m_model->setEntries(m_recentProjects->entries());
}

void ProjectsPage::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void ProjectsPage::retranslateUi() {
    if (m_search) {
        m_search->setPlaceholderText(QCoreApplication::translate("tlm::SearchField", "Search"));
    }
    if (m_sort) {
        const int current = m_sort->currentIndex();
        const QSignalBlocker blocker(m_sort);
        m_sort->clear();
        m_sort->addItems({tr("Last Edited"), tr("Name"), tr("Created"), tr("Path")});
        m_sort->setCurrentIndex(qBound(0, current, m_sort->count() - 1));
    }
    if (m_openButton) {
        m_openButton->setText(tr("Open"));
    }
    if (m_renameButton) {
        m_renameButton->setText(tr("Rename"));
    }
    if (m_coverButton) {
        m_coverButton->setText(tr("Choose Cover"));
    }
    if (m_revealButton) {
        m_revealButton->setText(tr("Reveal"));
    }
    if (m_duplicateButton) {
        m_duplicateButton->setText(tr("Duplicate"));
    }
    if (m_removeButton) {
        m_removeButton->setText(tr("Remove"));
    }
    if (m_deleteFileButton) {
        m_deleteFileButton->setText(tr("Delete File"));
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

QString ProjectsPage::selectedPath() const {
    return m_view->currentIndex().data(FilePathRole).toString();
}

void ProjectsPage::chooseCoverForSelectedProject() {
    const QString projectPath = selectedPath();
    if (projectPath.isEmpty() || !QFileInfo::exists(projectPath) || !m_repository) {
        return;
    }

    auto projectResult = m_repository->openProject(projectPath);
    if (!projectResult) {
        QMessageBox::warning(this, tr("Cover Image"),
                             projectResult.error().details.isEmpty()
                                 ? projectResult.error().message
                                 : QStringLiteral("%1\n\n%2").arg(projectResult.error().message,
                                                                   projectResult.error().details));
        return;
    }

    QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All Files (*)");
    const QString imagePath = QFileDialog::getOpenFileName(this, tr("Choose Project Cover"),
                                                           QFileInfo(projectPath).absolutePath(), filter);
    if (imagePath.isEmpty()) {
        return;
    }

    TierProject project = projectResult.takeValue();
    project.thumbnailPath = storedPathForProject(projectPath, imagePath);
    project.updatedAt = QDateTime::currentDateTimeUtc();
    project.dirty = true;
    auto saveResult = m_repository->saveProject(project, projectPath);
    if (!saveResult) {
        QMessageBox::warning(this, tr("Cover Image"),
                             saveResult.error().details.isEmpty()
                                 ? saveResult.error().message
                                 : QStringLiteral("%1\n\n%2").arg(saveResult.error().message,
                                                                   saveResult.error().details));
        return;
    }

    m_recentProjects->addOrUpdate(project);
    Logger::info(QStringLiteral("projects.cover.choose project=\"%1\" image=\"%2\"")
                     .arg(projectPath, project.thumbnailPath));
}

} // namespace tlm
