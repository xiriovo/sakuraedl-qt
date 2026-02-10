#include "performance_config.h"
#include "logger.h"
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace sakura {

PerformanceConfig::PerformanceConfig() {}

PerformanceConfig& PerformanceConfig::instance()
{
    static PerformanceConfig inst;
    return inst;
}

void PerformanceConfig::autoDetect()
{
    m_cpuCores = QThread::idealThreadCount();

#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        m_totalRamMB = static_cast<int>(memInfo.ullTotalPhys / (1024 * 1024));
    }
#else
    // Linux: read /proc/meminfo
    m_totalRamMB = 8192; // Default fallback
#endif

    bool lowPerf = (m_totalRamMB > 0 && m_totalRamMB < 8192) || m_cpuCores < 4;
    setLowPerformance(lowPerf);

    LOG_INFO(QString("Performance: CPU=%1 cores, RAM=%2 MB, LowPerf=%3")
                 .arg(m_cpuCores).arg(m_totalRamMB).arg(m_lowPerformance ? "YES" : "NO"));
}

void PerformanceConfig::setLowPerformance(bool low)
{
    if (m_lowPerformance != low) {
        m_lowPerformance = low;
        emit configChanged();
    }
}

} // namespace sakura
