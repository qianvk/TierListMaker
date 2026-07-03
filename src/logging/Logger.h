#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

namespace tlm {

/** Small logging facade for console/file diagnostics without logging image contents. */
class Logger : public QObject {
    Q_OBJECT

public:
    enum class Level { Debug, Info, Warn, Error };

    explicit Logger(QObject* parent = nullptr);
    ~Logger() override;

    void installMessageHandler();
    void log(Level level, const QString& message);

    static Logger* instance();
    static void debug(const QString& message);
    static void info(const QString& message);
    static void warn(const QString& message);
    static void error(const QString& message);

private:
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                 const QString& message);
    static QString levelName(Level level);

    QFile m_file;
    QMutex m_mutex;
    static Logger* s_instance;
};

} // namespace tlm

