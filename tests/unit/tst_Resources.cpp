#include <QtTest>

#include <vkui/core/VkIcon.h>

class tst_Resources : public QObject {
    Q_OBJECT

private slots:
    void iconsComeFromVkUi() {
        const QList symbols = {vkui::VkSymbol::Plus,     vkui::VkSymbol::Folder,
                               vkui::VkSymbol::Document, vkui::VkSymbol::Upload,
                               vkui::VkSymbol::Download, vkui::VkSymbol::Edit,
                               vkui::VkSymbol::Settings};
        for (const vkui::VkSymbol symbol : symbols) {
            const QIcon icon = vkui::icon(symbol);
            QVERIFY(!icon.isNull());
            QVERIFY(!icon.pixmap(QSize(24, 24)).isNull());
        }
    }
};

QTEST_MAIN(tst_Resources)
#include "tst_Resources.moc"
