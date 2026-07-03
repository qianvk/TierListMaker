#include "logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

namespace tlm {

Logger* Logger::s_instance = nullptr;

Logger::Logger(QObject* parent) : QObject(parent) {
    s_instance = this;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(QDir(base).filePath(QStringLiteral("logs")));
    m_file.setFileName(QDir(base).filePath(QStringLiteral("logs/tierlistmaker.log")));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_file.setFileName(QString());
    }
}

Logger::~Logger() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void Logger::installMessageHandler() {
    qInstallMessageHandler(&Logger::qtMessageHandler);
}

void Logger::log(Level level, const QString& message) {
    QMutexLocker locker(&m_mutex);
    const QString line =
        QStringLiteral("%1 [%2] %3\n")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), levelName(level), message);
#ifndef NDEBUG
    QTextStream(stderr) << line;
#endif
    if (m_file.isOpen()) {
        QTextStream(&m_file) << line;
        m_file.flush();
    }
}

Logger* Logger::instance() {
    return s_instance;
}

void Logger::debug(const QString& message) {
    if (s_instance) {
        s_instance->log(Level::Debug, message);
    }
}

void Logger::info(const QString& message) {
    if (s_instance) {
        s_instance->log(Level::Info, message);
    }
}

void Logger::warn(const QString& message) {
    if (s_instance) {
        s_instance->log(Level::Warn, message);
    }
}

void Logger::error(const QString& message) {
    if (s_instance) {
        s_instance->log(Level::Error, message);
    }
}

void Logger::qtMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    Level level = Level::Info;
    switch (type) {
    case QtDebugMsg:
        level = Level::Debug;
        break;
    case QtInfoMsg:
        level = Level::Info;
        break;
    case QtWarningMsg:
        level = Level::Warn;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = Level::Error;
        break;
    }
    if (s_instance) {
        s_instance->log(level, message);
    }
}

QString Logger::levelName(Level level) {
    switch (level) {
    case Level::Debug:
        return QStringLiteral("debug");
    case Level::Info:
        return QStringLiteral("info");
    case Level::Warn:
        return QStringLiteral("warn");
    case Level::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

} // namespace tlm
