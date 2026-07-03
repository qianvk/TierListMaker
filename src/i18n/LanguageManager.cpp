#include "i18n/LanguageManager.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>

namespace tlm {

LanguageManager::LanguageManager(QApplication* app, QObject* parent) : QObject(parent), m_app(app) {}

QStringList LanguageManager::availableLanguages() const {
    return {QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("zh_CN")};
}

bool LanguageManager::setLanguage(const QString& languageCode) {
    if (!m_app) {
        return false;
    }
    const QString requestedLanguage = languageCode.isEmpty() ? QStringLiteral("system") : languageCode;
    const QString resolvedLanguage =
        requestedLanguage == QStringLiteral("system")
            ? (QLocale::system().name().startsWith(QStringLiteral("zh")) ? QStringLiteral("zh_CN")
                                                                          : QStringLiteral("en"))
            : requestedLanguage;

    m_app->removeTranslator(&m_translator);
    m_app->removeTranslator(&m_qtTranslator);
    bool loaded = true;
    if (resolvedLanguage == QStringLiteral("zh_CN")) {
        const bool qtLoaded = m_qtTranslator.load(QStringLiteral("qtbase_zh_CN"),
                                                  QLibraryInfo::path(QLibraryInfo::TranslationsPath));
        if (qtLoaded) {
            m_app->installTranslator(&m_qtTranslator);
        }
        loaded = m_translator.load(QStringLiteral(":/translations/TierListMaker_zh_CN.qm"));
        if (loaded) {
            m_app->installTranslator(&m_translator);
        }
    }
    m_currentLanguage = requestedLanguage;
    emit languageChanged(requestedLanguage);
    return loaded || resolvedLanguage == QStringLiteral("en");
}

} // namespace tlm
