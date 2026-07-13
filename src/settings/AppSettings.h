#pragma once

#include <QObject>
#include <QDateTime>
#include <QSettings>
#include <QString>

namespace tlm {

enum class AppearanceMode { System, Light, Dark };
enum class ImageImportBehavior { CopyIntoProject, ReferenceOriginal, AskEveryTime };
enum class BlankAreaAction { None, TierMissionControl, GalleryMissionControl };

/** Persistent application preferences backed by QSettings. */
class AppSettings : public QObject {
    Q_OBJECT

public:
    explicit AppSettings(QObject* parent = nullptr);

    QString language() const;
    void setLanguage(const QString& language);

    AppearanceMode appearance() const;
    void setAppearance(AppearanceMode mode);

    ImageImportBehavior importBehavior() const;
    void setImportBehavior(ImageImportBehavior behavior);

    bool autosaveEnabled() const;
    void setAutosaveEnabled(bool enabled);
    int autosaveIntervalMinutes() const;
    void setAutosaveIntervalMinutes(int minutes);
    QString defaultProjectDirectory() const;
    void setDefaultProjectDirectory(const QString& path);

    bool autoUpdateEnabled() const;
    void setAutoUpdateEnabled(bool enabled);
    QDateTime lastUpdateCheckAt() const;
    void setLastUpdateCheckAt(const QDateTime& checkedAt);
    bool shouldRunAutoUpdateCheck(const QDateTime& now = QDateTime::currentDateTimeUtc()) const;

    BlankAreaAction blankDoubleClickAction() const;
    void setBlankDoubleClickAction(BlankAreaAction action);
    BlankAreaAction blankLongPressAction() const;
    void setBlankLongPressAction(BlankAreaAction action);

    QString defaultExportFormat() const;
    void setDefaultExportFormat(const QString& format);
    int defaultExportScale() const;
    void setDefaultExportScale(int scale);

    bool animationsEnabled() const;
    void setAnimationsEnabled(bool enabled);
    bool reducedMotion() const;
    void setReducedMotion(bool enabled);

    bool localOnlyMode() const;
    void setLocalOnlyMode(bool enabled);

signals:
    void changed();
    void languageChanged(const QString& language);
    void appearanceChanged(AppearanceMode mode);
    void autoUpdateEnabledChanged(bool enabled);
    void blankDoubleClickActionChanged(BlankAreaAction action);
    void blankLongPressActionChanged(BlankAreaAction action);

private:
    QSettings m_settings;
};

} // namespace tlm
