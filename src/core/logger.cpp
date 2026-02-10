#include "logger.h"
#include <QDir>
#include <QTextStream>
#include <QCoreApplication>
#include <iostream>

namespace sakura {

Logger::Logger() {}

Logger::~Logger()
{
    if (m_logFile.isOpen())
        m_logFile.close();
}

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

void Logger::initialize(const QString& logDir)
{
    QMutexLocker lock(&m_mutex);

    QString dir = logDir;
    if (dir.isEmpty()) {
        dir = QCoreApplication::applicationDirPath() + "/logs";
    }
    QDir().mkpath(dir);

    QString filename = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".log";
    m_logFilePath = dir + "/" + filename;

    m_logFile.setFileName(m_logFilePath);
    m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    info("Logger initialized: " + m_logFilePath);
}

void Logger::setMinLevel(LogLevel level)
{
    m_minLevel = level;
}

void Logger::setUILogger(std::function<void(const QString&, LogLevel)> callback)
{
    m_uiCallback = std::move(callback);
}

void Logger::debug(const QString& msg, const QString& category)
{
    log(LogLevel::Debug, msg, category);
}

void Logger::info(const QString& msg, const QString& category)
{
    log(LogLevel::Info, msg, category);
}

void Logger::warning(const QString& msg, const QString& category)
{
    log(LogLevel::Warning, msg, category);
}

void Logger::error(const QString& msg, const QString& category)
{
    log(LogLevel::Error, msg, category);
}

void Logger::fatal(const QString& msg, const QString& category)
{
    log(LogLevel::Fatal, msg, category);
}

void Logger::log(LogLevel level, const QString& msg, const QString& category)
{
    if (level < m_minLevel)
        return;

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString levelStr = levelToString(level);
    QString catStr = category.isEmpty() ? "" : ("[" + category + "] ");
    QString formatted = QString("[%1] [%2] %3%4").arg(timestamp, levelStr, catStr, msg);

    {
        QMutexLocker lock(&m_mutex);
        m_latestMessage = formatted;
        writeToFile(formatted);
    }

    // Console output
    std::cout << formatted.toStdString() << std::endl;

    // UI callback
    if (m_uiCallback)
        m_uiCallback(formatted, level);

    emit messageLogged(formatted, static_cast<int>(level));
}

void Logger::writeToFile(const QString& formatted)
{
    if (m_logFile.isOpen()) {
        QTextStream stream(&m_logFile);
        stream << formatted << "\n";
        stream.flush();
    }
}

QString Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    case LogLevel::Fatal:   return "FATAL";
    }
    return "UNKNOWN";
}

QColor Logger::levelToColor(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:   return QColor(128, 128, 128);
    case LogLevel::Info:    return QColor(0, 150, 0);
    case LogLevel::Warning: return QColor(200, 150, 0);
    case LogLevel::Error:   return QColor(220, 50, 50);
    case LogLevel::Fatal:   return QColor(200, 0, 0);
    }
    return QColor(0, 0, 0);
}

} // namespace sakura
