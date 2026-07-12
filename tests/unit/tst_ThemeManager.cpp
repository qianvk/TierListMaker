#include "settings/AppSettings.h"
#include "theme/Theme.h"
#include "theme/ThemeManager.h"

#include <QApplication>
#include <QStandardPaths>
#include <QtTest>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkThemeManager.h>

using namespace tlm;

class tst_ThemeManager : public QObject {
    Q_OBJECT

private slots:
    void lightAndDarkHaveDistinctTokens() {
        auto* manager = vkui::VkThemeManager::instance();
        const vkui::VkAppearance original = manager->appearance();
        manager->setAppearance(vkui::VkAppearance::Light);
        const Theme light(manager->theme());
        const QColor lightAccent = manager->theme().colors().accent;
        QCOMPARE(activeThemeTokens().contentBackground,
                 manager->theme().colors().contentBackground);
        QCOMPARE(qApp->palette().color(QPalette::Base),
                 manager->theme().colors().contentBackground);
        QCOMPARE(light.tokens().popoverBackground, manager->theme().colors().popoverBackground);
        manager->setAppearance(vkui::VkAppearance::Dark);
        const Theme dark(manager->theme());
        QVERIFY(light.tokens().contentBackground != dark.tokens().contentBackground);
        QCOMPARE(light.tokens().accent, lightAccent);
        QCOMPARE(activeThemeTokens().tierRowBackground,
                 manager->theme().colors().elevatedBackground);
        QCOMPARE(dark.tokens().separator, manager->theme().colors().separator);
        QCOMPARE(dark.tokens().imageBorder, manager->theme().colors().border);
        QCOMPARE(qApp->palette().color(QPalette::Window),
                 manager->theme().colors().windowBackground);
        QVERIFY(dark.isDark());
        QVERIFY(!light.isDark());
        QVERIFY(dark.tokens().primaryText.lightness() >
                dark.tokens().contentBackground.lightness());
        QVERIFY(light.tokens().primaryText.lightness() <
                light.tokens().contentBackground.lightness());
        manager->setAppearance(original);
    }

    void settingsDriveTheVkUiTheme() {
        QStandardPaths::setTestModeEnabled(true);
        AppSettings settings;
        ThemeManager bridge(&settings);

        settings.setAppearance(AppearanceMode::Dark);
        bridge.applyTo(*qApp);
        QCOMPARE(vkui::VkThemeManager::instance()->effectiveAppearance(), vkui::VkAppearance::Dark);
        QCOMPARE(bridge.tokens().contentBackground,
                 vkui::VkThemeManager::instance()->theme().colors().contentBackground);

        settings.setAppearance(AppearanceMode::Light);
        QCOMPARE(vkui::VkThemeManager::instance()->effectiveAppearance(),
                 vkui::VkAppearance::Light);
        QCOMPARE(qApp->palette().color(QPalette::Base), bridge.tokens().contentBackground);
        settings.setAppearance(AppearanceMode::System);
    }
};

QTEST_MAIN(tst_ThemeManager)
#include "tst_ThemeManager.moc"
