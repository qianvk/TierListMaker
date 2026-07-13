#pragma once

#include "window/AppDialog.h"

class QCheckBox;
class QLabel;
class QLineEdit;

namespace tlm {

/** Project save location dialog with the same folder-first flow as the original new-project sheet. */
class ProjectLocationDialog final : public AppDialog {
    Q_OBJECT

public:
    ProjectLocationDialog(const QString& projectName, const QString& parentDirectory,
                          const QString& defaultDirectory, QWidget* parent = nullptr);

    QString projectName() const;
    QString parentDirectory() const;
    QString projectFilePath() const;
    bool shouldUseAsDefaultDirectory() const;

private:
    void chooseParentDirectory();
    void refreshPreview();
    static QString sanitizedFileStem(const QString& value);

    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_directoryEdit{nullptr};
    QLabel* m_pathPreview{nullptr};
    QCheckBox* m_defaultDirectoryCheck{nullptr};
};

} // namespace tlm
