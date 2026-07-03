#include "theme/Theme.h"

#include <QtTest>

using namespace tlm;

class tst_ThemeManager : public QObject {
    Q_OBJECT

private slots:
    void lightAndDarkHaveDistinctTokens() {
        Theme light(Theme::Kind::Light);
        Theme dark(Theme::Kind::Dark);
        QVERIFY(light.tokens().contentBackground != dark.tokens().contentBackground);
        QVERIFY(dark.isDark());
        QVERIFY(!light.isDark());
    }
};

QTEST_MAIN(tst_ThemeManager)
#include "tst_ThemeManager.moc"

