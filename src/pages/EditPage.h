#pragma once

#include "assets/AssetManager.h"
#include "assets/ThumbnailCache.h"
#include "export/TierListExporter.h"
#include "persistence/ProjectRepository.h"
#include "persistence/RecentProjectsStore.h"
#include "settings/AppSettings.h"
#include "tier/TierProject.h"

#include <QPointer>
#include <QWidget>

class QTimer;
class QVBoxLayout;

namespace vkui {
class VkPopover;
}

namespace tlm {

class ImageGalleryPopover;
class PreviewOverlay;
class TierBoardWidget;

/** Main editing workspace for the active tier-list project. */
class EditPage : public QWidget {
    Q_OBJECT

public:
    EditPage(ProjectRepository* repository, RecentProjectsStore* recentProjects,
             AssetManager* assetManager, ThumbnailCache* thumbnailCache, AppSettings* settings,
             QWidget* parent = nullptr);

    const TierProject& project() const {
        return m_project;
    }
    bool isDirty() const {
        return m_project.dirty;
    }
    QString displayTitle() const;

public slots:
    bool newProject();
    bool openProjectFromDialog();
    bool openProject(const QString& filePath);
    bool saveProject();
    bool saveProjectAs();
    void renameProject(const QString& name);
    void resetRows();
    void importImagesFromDialog();
    void importImages(const QStringList& filePaths);
    void exportProjectFromDialog();
    void showTemplateMenu(QWidget* anchor = nullptr);
    void configureBackground(QWidget* anchor = nullptr);
    void deleteSelectedImage();
    void previewSelectedImage();
    bool confirmSaveIfDirty();
    void setTierFocusMode(bool enabled);
    void toggleMissionControlMode();
    void toggleGallery(QWidget* anchor = nullptr);
    void toggleGalleryMissionControlMode(const QRect& sourceGlobalRect = QRect());

signals:
    void titleChanged(const QString& title);
    void dirtyChanged(bool dirty);
    void resetRowsAvailableChanged(bool available);
    void projectSaved();
    void projectOpened(const QString& filePath);
    void galleryMissionControlRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void refreshUi();
    void markDirty();
    void setProject(TierProject project);
    void showError(const QString& title, const Error& error);
    QString chooseSavePath();
    QString chooseTemplatePath(bool saveDialog);
    QStringList chooseImageImportFiles(QWidget* dialogParent);
    void closeTransientPopovers();
    bool ensureProjectFile();
    TierProject createProjectFromDefaultTemplate() const;
    QString uniqueDefaultProjectPath(QString* projectName = nullptr) const;
    bool autosaveCurrentProject();
    bool saveTemplateFromDialog();
    bool applyTemplateFromDialog();
    bool saveTemplateToPath(const QString& path);
    bool saveManagedTemplateFromPrompt(QWidget* reopenAnchor);
    QString managedTemplateDirectory() const;
    TierProject templateSnapshot() const;
    void applyTemplateProject(const TierProject& templateProject);
    void moveImageToRow(const QString& imageId, const QString& rowId, int index);
    void moveRowToIndex(const QString& rowId, int destinationIndex);
    void editImage(const QString& imageId);
    void removeImageFromTierRow(const QString& imageId);
    void removeImageFromGallery(const QString& imageId);
    void editTierRow(const QString& rowId);
    void clearTierRowImages(const QString& rowId);
    void deleteTierRow(const QString& rowId);
    void insertTierRow(const QString& rowId, bool below);
    bool saveProjectToPath(const QString& filePath);
    void removeImageFromRows(const QString& imageId);
    void layoutOverlays();
    bool hasImagesInRows() const;
    QPixmap pixmapForImage(const QString& imageId) const;

    ProjectRepository* m_repository{nullptr};
    RecentProjectsStore* m_recentProjects{nullptr};
    AssetManager* m_assetManager{nullptr};
    ThumbnailCache* m_thumbnailCache{nullptr};
    AppSettings* m_settings{nullptr};
    TierListExporter* m_exporter{nullptr};
    TierProject m_project;
    QString m_selectedImageId;

    QVBoxLayout* m_rootLayout{nullptr};
    TierBoardWidget* m_board{nullptr};
    QPointer<ImageGalleryPopover> m_galleryPopover;
    QPointer<vkui::VkPopover> m_templatePopover;
    PreviewOverlay* m_previewOverlay{nullptr};
    QTimer* m_autosaveTimer{nullptr};
    bool m_tierFocusMode{false};
    bool m_backgroundPreviewActive{false};
};

} // namespace tlm
