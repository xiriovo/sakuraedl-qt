#pragma once

#include <QObject>

namespace sakura {

class PerformanceConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool lowPerformanceMode READ isLowPerformance NOTIFY configChanged)
    Q_PROPERTY(int uiRefreshIntervalMs READ uiRefreshIntervalMs NOTIFY configChanged)
    Q_PROPERTY(int animationFps READ animationFps NOTIFY configChanged)

public:
    static PerformanceConfig& instance();

    void autoDetect();

    bool isLowPerformance() const { return m_lowPerformance; }
    int uiRefreshIntervalMs() const { return m_lowPerformance ? 500 : 100; }
    int animationFps() const { return m_lowPerformance ? 15 : 60; }
    int totalRamMB() const { return m_totalRamMB; }
    int cpuCores() const { return m_cpuCores; }

    void setLowPerformance(bool low);

signals:
    void configChanged();

private:
    PerformanceConfig();

    bool m_lowPerformance = false;
    int m_totalRamMB = 0;
    int m_cpuCores = 0;
};

} // namespace sakura
