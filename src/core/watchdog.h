#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QMutex>
#include <functional>

namespace sakura {

enum class WatchdogState {
    Idle,
    Running,
    TimedOut,
    Stopped
};

class Watchdog : public QObject {
    Q_OBJECT

public:
    explicit Watchdog(int timeoutMs = 30000, QObject* parent = nullptr);
    ~Watchdog();

    void start(const QString& operationName = QString());
    void stop();
    void feed();
    bool isRunning() const;
    bool isTimedOut() const;

    void setTimeoutMs(int ms);
    int timeoutMs() const { return m_timeoutMs; }
    int elapsedMs() const;
    QString operationName() const { return m_operationName; }
    WatchdogState state() const { return m_state; }

signals:
    void timeout(const QString& operationName, int elapsedMs);
    void stateChanged(int state);

private slots:
    void onTimerTick();

private:
    int m_timeoutMs;
    QString m_operationName;
    WatchdogState m_state = WatchdogState::Idle;
    QElapsedTimer m_elapsed;
    QTimer m_checkTimer;
    QMutex m_mutex;
    qint64 m_lastFeedTime = 0;
};

// RAII scope guard for watchdog
class WatchdogScope {
public:
    WatchdogScope(Watchdog& wd, const QString& op, int timeoutMs = -1)
        : m_watchdog(wd)
    {
        if (timeoutMs > 0)
            m_watchdog.setTimeoutMs(timeoutMs);
        m_watchdog.start(op);
    }
    ~WatchdogScope() { m_watchdog.stop(); }

    WatchdogScope(const WatchdogScope&) = delete;
    WatchdogScope& operator=(const WatchdogScope&) = delete;

private:
    Watchdog& m_watchdog;
};

} // namespace sakura
