#include "watchdog.h"
#include "logger.h"

namespace sakura {

Watchdog::Watchdog(int timeoutMs, QObject* parent)
    : QObject(parent), m_timeoutMs(timeoutMs)
{
    m_checkTimer.setInterval(500);
    connect(&m_checkTimer, &QTimer::timeout, this, &Watchdog::onTimerTick);
}

Watchdog::~Watchdog()
{
    stop();
}

void Watchdog::start(const QString& operationName)
{
    QMutexLocker lock(&m_mutex);
    m_operationName = operationName;
    m_state = WatchdogState::Running;
    m_elapsed.start();
    m_lastFeedTime = 0;
    m_checkTimer.start();
    emit stateChanged(static_cast<int>(m_state));
}

void Watchdog::stop()
{
    QMutexLocker lock(&m_mutex);
    m_checkTimer.stop();
    m_state = WatchdogState::Stopped;
    emit stateChanged(static_cast<int>(m_state));
}

void Watchdog::feed()
{
    QMutexLocker lock(&m_mutex);
    m_lastFeedTime = m_elapsed.elapsed();
}

bool Watchdog::isRunning() const
{
    return m_state == WatchdogState::Running;
}

bool Watchdog::isTimedOut() const
{
    return m_state == WatchdogState::TimedOut;
}

void Watchdog::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
}

int Watchdog::elapsedMs() const
{
    return m_elapsed.isValid() ? static_cast<int>(m_elapsed.elapsed()) : 0;
}

void Watchdog::onTimerTick()
{
    QMutexLocker lock(&m_mutex);
    if (m_state != WatchdogState::Running)
        return;

    qint64 sinceLastFeed = m_elapsed.elapsed() - m_lastFeedTime;
    if (sinceLastFeed >= m_timeoutMs) {
        m_state = WatchdogState::TimedOut;
        m_checkTimer.stop();
        LOG_WARNING(QString("Watchdog timeout: %1 (elapsed: %2ms)")
                        .arg(m_operationName)
                        .arg(m_elapsed.elapsed()));
        emit timeout(m_operationName, static_cast<int>(m_elapsed.elapsed()));
        emit stateChanged(static_cast<int>(m_state));
    }
}

} // namespace sakura
