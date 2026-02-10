#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <memory>

namespace sakura {

class SpreadtrumService;
class ITransport;

class SpreadtrumController : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(int deviceState READ deviceState NOTIFY deviceStateChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY portChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool isWatching READ isWatching NOTIFY deviceStateChanged)
    Q_PROPERTY(int language READ language WRITE setLanguage NOTIFY languageChanged)

    // Readiness
    Q_PROPERTY(bool pacReady READ pacReady NOTIFY readinessChanged)
    Q_PROPERTY(bool fdl1Ready READ fdl1Ready NOTIFY readinessChanged)
    Q_PROPERTY(bool fdl2Ready READ fdl2Ready NOTIFY readinessChanged)
    Q_PROPERTY(bool fdlReady READ fdlReady NOTIFY readinessChanged)
    Q_PROPERTY(bool isDeviceReady READ isDeviceReady NOTIFY deviceStateChanged)
    Q_PROPERTY(bool hasCheckedPartitions READ hasCheckedPartitions NOTIFY partitionsChanged)
    Q_PROPERTY(QString statusHint READ statusHint NOTIFY readinessChanged)
    Q_PROPERTY(QString pacPath READ pacPath NOTIFY readinessChanged)
    Q_PROPERTY(QString fdl1Address READ fdl1Address WRITE setFdl1Address NOTIFY readinessChanged)
    Q_PROPERTY(QString fdl2Address READ fdl2Address WRITE setFdl2Address NOTIFY readinessChanged)

    // Partitions
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY partitionsChanged)
    Q_PROPERTY(int firmwareEntryCount READ firmwareEntryCount NOTIFY partitionsChanged)

    // Device info
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString chipName READ chipName NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString chipId READ chipId NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString flashType READ flashType NOTIFY deviceInfoChanged)

    // Progress
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY progressChanged)

public:
    enum DeviceState { Disconnected=0, Scanning, Connected, Fdl1Loaded, Fdl2Loaded, Ready, Error };
    Q_ENUM(DeviceState)

    explicit SpreadtrumController(QObject* parent = nullptr);
    ~SpreadtrumController() override;

    // Getters
    int deviceState() const { return m_deviceState; }
    QString portName() const { return m_portName; }
    bool isBusy() const { return m_busy; }
    bool isWatching() const { return m_watching; }
    int language() const { return m_language; }
    void setLanguage(int v) { if(m_language!=v){m_language=v; emit languageChanged(); emit readinessChanged();} }
    bool pacReady() const { return m_pacReady; }
    bool fdl1Ready() const { return m_fdl1Ready; }
    bool fdl2Ready() const { return m_fdl2Ready; }
    bool fdlReady() const { return m_fdl1Ready && m_fdl2Ready; }
    bool isDeviceReady() const { return m_deviceState >= Ready; }
    bool hasCheckedPartitions() const { return m_checkedCount > 0; }
    QString statusHint() const;
    QString pacPath() const { return m_pacPath; }
    QString fdl1Address() const { return m_fdl1Address; }
    void setFdl1Address(const QString& addr) { if(m_fdl1Address!=addr){m_fdl1Address=addr; emit readinessChanged();} }
    QString fdl2Address() const { return m_fdl2Address; }
    void setFdl2Address(const QString& addr) { if(m_fdl2Address!=addr){m_fdl2Address=addr; emit readinessChanged();} }
    QVariantList partitions() const { return m_partitions; }
    int firmwareEntryCount() const { return m_checkedCount; }
    QVariantMap deviceInfo() const { return m_deviceInfo; }
    QString chipName() const { return m_deviceInfo.value("chip","-").toString(); }
    QString chipId() const { return m_deviceInfo.value("chipId","-").toString(); }
    QString flashType() const { return m_deviceInfo.value("flash","-").toString(); }
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString speedText() const { return m_speedText; }
    QString etaText() const { return m_etaText; }
    QString elapsedText() const { return m_elapsedText; }

    // Actions
    Q_INVOKABLE void loadPacFile(const QString& path);
    Q_INVOKABLE void loadFdl1File(const QString& path);
    Q_INVOKABLE void loadFdl2File(const QString& path);
    Q_INVOKABLE void loadFirmwareDir(const QString& dirPath);
    Q_INVOKABLE void startAutoDetect();
    Q_INVOKABLE void stopAutoDetect();
    Q_INVOKABLE void stopOperation();
    Q_INVOKABLE void disconnect();

    // Operations (need device ready)
    Q_INVOKABLE void readPartitionTable();
    Q_INVOKABLE void flashPac();
    Q_INVOKABLE void readFlash();
    Q_INVOKABLE void writeFlash();
    Q_INVOKABLE void eraseFlash();
    Q_INVOKABLE void readImei();
    Q_INVOKABLE void writeImei(const QString& imei1, const QString& imei2);
    Q_INVOKABLE void readNv();
    Q_INVOKABLE void writeNv(const QString& nvPath);
    Q_INVOKABLE void unlockBootloader();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();

    // Partition management
    Q_INVOKABLE void togglePartition(int index);
    Q_INVOKABLE void selectAll(bool checked);

signals:
    void deviceStateChanged();
    void portChanged();
    void busyChanged();
    void readinessChanged();
    void partitionsChanged();
    void deviceInfoChanged();
    void progressChanged();
    void languageChanged();
    void operationCompleted(bool success, const QString& message);
    void logMessage(const QString& msg);

protected:
    void timerEvent(QTimerEvent* ev) override;

private:
    void setDeviceState(int state);
    void setBusy(bool busy);
    void addLog(const QString& msg);
    void addLogOk(const QString& msg);
    void addLogErr(const QString& msg);
    void addLogFail(const QString& msg);
    bool zh() const { return m_language==0; }
    QString L(const char* z, const char* e) const;
    void updateProgress(qint64 cur, qint64 tot, const QString& label);
    void resetProgress();
    void tryStartAutoDetect();
    void connectDevice(const QString& port);

    std::unique_ptr<SpreadtrumService> m_service;
    std::unique_ptr<ITransport> m_ownedTransport;  // Transport ownership

    int m_deviceState = Disconnected;
    int m_language = 0;
    QString m_portName;
    bool m_busy = false;
    bool m_watching = false;
    int m_watchTimerId = 0;

    bool m_pacReady = false;
    bool m_fdl1Ready = false;
    bool m_fdl2Ready = false;
    QString m_pacPath;
    QString m_fdl1Path;
    QString m_fdl2Path;
    QString m_fdl1Address = "0x65000800";
    QString m_fdl2Address = "0x9EFFFE00";

    QVariantList m_partitions;
    int m_checkedCount = 0;
    QVariantMap m_deviceInfo;

    double m_progress = 0.0;
    QString m_progressText, m_speedText, m_etaText, m_elapsedText;
    qint64 m_progressStartMs=0, m_lastSpeedMs=0, m_lastSpeedBytes=0;
};

} // namespace sakura
