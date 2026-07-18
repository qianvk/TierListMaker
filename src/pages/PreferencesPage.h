#pragma once

#include "i18n/LanguageManager.h"
#include "settings/AppSettings.h"
#include "update/AppUpdater.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QEvent;
class QResizeEvent;
class QStackedWidget;

namespace tlm {

/** Non-modal preferences page with General, Updates, and About sections. */
class PreferencesPage : public QWidget {
    Q_OBJECT

public:
    PreferencesPage(AppSettings* settings, LanguageManager* languageManager, AppUpdater* updater,
                    QWidget* parent = nullptr);
    void setUpdateNotificationVisible(bool visible);

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void retranslateUi();
    void rebuildPreferencePages();
    void updateNavWidth();
    void refreshPreferenceControlStyles();
    void refreshUpdateActions();
    void applyUpdateResult(const UpdateCheckResult& result);
    void applyUpdateFailure(const QString& reason);
    QWidget* createGeneralPage();
    QWidget* createUpdatePage();
    QWidget* createAboutPage();

    AppSettings* m_settings{nullptr};
    LanguageManager* m_languageManager{nullptr};
    AppUpdater* m_updater{nullptr};
    QListWidget* m_nav{nullptr};
    QStackedWidget* m_stack{nullptr};
    QLabel* m_updateStatusLabel{nullptr};
    QPushButton* m_checkUpdateButton{nullptr};
    QPushButton* m_installUpdateButton{nullptr};
    QPushButton* m_openUpdateButton{nullptr};
    UpdateCheckResult m_lastUpdateResult;
};

} // namespace tlm
