#include "pages/ProjectLocationDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <vkui/core/VkIcon.h>

namespace tlm {

namespace {
QString documentsOrHome() {
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}
} // namespace

ProjectLocationDialog::ProjectLocationDialog(const QString& projectName,
                                             const QString& parentDirectory,
                                             const QString& defaultDirectory, QWidget* parent)
    : AppDialog(QObject::tr("Save Project"), parent), m_nameEdit(new QLineEdit(this)),
      m_directoryEdit(new QLineEdit(this)), m_pathPreview(new QLabel(this)),
      m_defaultDirectoryCheck(new QCheckBox(tr("Use this folder as the default project folder"), this)) {
    setObjectName(QStringLiteral("ProjectLocationDialog"));
    setMinimumWidth(520);

    const QString initialDirectory =
        parentDirectory.isEmpty() ? (defaultDirectory.isEmpty() ? documentsOrHome()
                                                                : defaultDirectory)
                                  : parentDirectory;

    m_nameEdit->setText(projectName.trimmed().isEmpty() ? tr("Untitled Tier List") : projectName);
    m_nameEdit->setClearButtonEnabled(true);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &ProjectLocationDialog::refreshPreview);

    m_directoryEdit->setText(QDir::toNativeSeparators(initialDirectory));
    m_directoryEdit->setReadOnly(true);
    connect(m_directoryEdit, &QLineEdit::textChanged, this, &ProjectLocationDialog::refreshPreview);

    auto* browseButton = new QPushButton(vkui::icon(vkui::VkSymbol::Folder), tr("Choose"), this);
    connect(browseButton, &QPushButton::clicked, this, &ProjectLocationDialog::chooseParentDirectory);

    auto* directoryRow = new QWidget(this);
    auto* directoryLayout = new QHBoxLayout(directoryRow);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    directoryLayout->setSpacing(8);
    directoryLayout->addWidget(m_directoryEdit, 1);
    directoryLayout->addWidget(browseButton);

    m_pathPreview->setWordWrap(true);
    m_pathPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);

    const QString normalizedInitial = QFileInfo(initialDirectory).absoluteFilePath();
    const QString normalizedDefault =
        defaultDirectory.isEmpty() ? QString() : QFileInfo(defaultDirectory).absoluteFilePath();
    m_defaultDirectoryCheck->setChecked(!normalizedDefault.isEmpty() &&
                                        normalizedInitial.compare(normalizedDefault,
                                                                  Qt::CaseInsensitive) == 0);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("Project name"), m_nameEdit);
    form->addRow(tr("Parent folder"), directoryRow);
    form->addRow(tr("Save path"), m_pathPreview);
    contentLayout()->addLayout(form);
    contentLayout()->addWidget(m_defaultDirectoryCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (this->projectName().isEmpty()) {
            QMessageBox::warning(this, tr("Save Project"), tr("Enter a project name."));
            return;
        }
        if (this->parentDirectory().isEmpty()) {
            QMessageBox::warning(this, tr("Save Project"), tr("Choose a parent folder."));
            return;
        }
        accept();
    });
    contentLayout()->addWidget(buttons);

    refreshPreview();
    QTimer::singleShot(0, this, [this]() {
        m_nameEdit->setFocus(Qt::OtherFocusReason);
        m_nameEdit->selectAll();
    });
}

QString ProjectLocationDialog::projectName() const {
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString ProjectLocationDialog::parentDirectory() const {
    const QString path = m_directoryEdit ? QDir::fromNativeSeparators(m_directoryEdit->text()) : QString();
    return path.trimmed();
}

QString ProjectLocationDialog::projectFilePath() const {
    const QString parent = parentDirectory();
    if (parent.isEmpty()) {
        return {};
    }
    const QString stem = sanitizedFileStem(projectName());
    return QDir(QDir(parent).filePath(stem)).filePath(stem + QStringLiteral(".tlmproject"));
}

bool ProjectLocationDialog::shouldUseAsDefaultDirectory() const {
    return m_defaultDirectoryCheck && m_defaultDirectoryCheck->isChecked();
}

void ProjectLocationDialog::chooseParentDirectory() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Choose Parent Folder"),
        parentDirectory().isEmpty() ? documentsOrHome() : parentDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) {
        return;
    }
    m_directoryEdit->setText(QDir::toNativeSeparators(path));
}

void ProjectLocationDialog::refreshPreview() {
    if (!m_pathPreview) {
        return;
    }
    m_pathPreview->setText(QDir::toNativeSeparators(projectFilePath()));
}

QString ProjectLocationDialog::sanitizedFileStem(const QString& value) {
    QString stem = value.trimmed();
    stem.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")), QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    stem = stem.trimmed();
    while (stem.endsWith(u'.')) {
        stem.chop(1);
    }
    return stem.isEmpty() ? QObject::tr("Untitled Tier List") : stem;
}

} // namespace tlm
