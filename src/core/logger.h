#pragma once

#include <QObject>
#include <QString>
#include <QFile>
#include <QMutex>
#include <QDateTime>
#include <QColor>
#include <functional>

namespace sakura {

enum class LogLevel {
    Debug = 0,
    Info,
    Warning,
    Error,
    Fatal
};

class Logger : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString latestMessage READ latestMessage NOTIFY messageLogged)

public:
    static Logger& instance();

    void initialize(const QString& logDir = QString());
    void setMinLevel(LogLevel level);
    void setUILogger(std::function<void(const QString&, LogLevel)> callback);

    void debug(const QString& msg, const QString& category = QString());
    void info(const QString& msg, const QString& category = QString());
    void warning(const QString& msg, const QString& category = QString());
    void error(const QString& msg, const QString& category = QString());
    void fatal(const QString& msg, const QString& category = QString());

    void log(LogLevel level, const QString& msg, const QString& category = QString());

    QString latestMessage() const { return m_latestMessage; }
    QString logFilePath() const { return m_logFilePath; }

    static QString levelToString(LogLevel level);
    static QColor levelToColor(LogLevel level);

signals:
    void messageLogged(const QString& message, int level);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writeToFile(const QString& formatted);

    QFile m_logFile;
    QString m_logFilePath;
    QString m_latestMessage;
    LogLevel m_minLevel = LogLevel::Debug;
    QMutex m_mutex;
    std::function<void(const QString&, LogLevel)> m_uiCallback;
};

// Convenience macros
#define LOG_DEBUG(msg)   sakura::Logger::instance().debug(msg)
#define LOG_INFO(msg)    sakura::Logger::instance().info(msg)
#define LOG_WARNING(msg) sakura::Logger::instance().warning(msg)
#define LOG_ERROR(msg)   sakura::Logger::instance().error(msg)
#define LOG_FATAL(msg)   sakura::Logger::instance().fatal(msg)

#define LOG_DEBUG_CAT(cat, msg)   sakura::Logger::instance().debug(msg, cat)
#define LOG_INFO_CAT(cat, msg)    sakura::Logger::instance().info(msg, cat)
#define LOG_WARNING_CAT(cat, msg) sakura::Logger::instance().warning(msg, cat)
#define LOG_ERROR_CAT(cat, msg)   sakura::Logger::instance().error(msg, cat)

} // namespace sakura
