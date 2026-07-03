#pragma once

#include <QObject>
#include <QTranslator>

class QApplication;

namespace tlm {

/** Loads Qt translators and emits a signal so widgets can refresh visible strings. */
class LanguageManager : public QObject {
    Q_OBJECT

public:
    explicit LanguageManager(QApplication* app, QObject* parent = nullptr);

    QString currentLanguage() const { return m_currentLanguage; }
    QStringList availableLanguages() const;
    bool setLanguage(const QString& languageCode);

signals:
    void languageChanged(const QString& languageCode);

private:
    QApplication* m_app{nullptr};
    QTranslator m_translator;
    QTranslator m_qtTranslator;
    QString m_currentLanguage{QStringLiteral("en")};
};

} // namespace tlm
