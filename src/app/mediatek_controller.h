#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <memory>

namespace sakura {

class MediatekService;
class ITransport;

class MediatekController : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(int deviceState READ deviceState NOTIFY deviceStateChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY portChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool isWatching READ isWatching NOTIFY deviceStateChanged)
    Q_PROPERTY(int language READ language WRITE setLanguage NOTIFY languageChanged)

    // Readiness
    Q_PROPERTY(bool daReady READ daReady NOTIFY readinessChanged)
    Q_PROPERTY(bool scatterReady READ scatterReady NOTIFY readinessChanged)
    Q_PROPERTY(bool isDeviceReady READ isDeviceReady NOTIFY deviceStateChanged)
    Q_PROPERTY(bool hasCheckedPartitions READ hasCheckedPartitions NOTIFY partitionsChanged)
    Q_PROPERTY(QString statusHint READ statusHint NOTIFY readinessChanged)
    Q_PROPERTY(QString daPath READ daPath NOTIFY readinessChanged)

    // Partitions
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY partitionsChanged)
    Q_PROPERTY(int firmwareEntryCount READ firmwareEntryCount NOTIFY partitionsChanged)

    // Device info
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString chipName READ chipName NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString hwCode READ hwCode NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString meId READ meId NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString socId READ socId NOTIFY deviceInfoChanged)

    // Progress
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY progressChanged)

    // Protocol
    Q_PROPERTY(int protocolType READ protocolType WRITE setProtocolType NOTIFY protocolTypeChanged)

public:
    enum DeviceState { Disconnected=0, Scanning, Handshaking, BromMode, PreloaderMode, Da1Loaded, Da2Loaded, Ready, Error };
    Q_ENUM(DeviceState)
    enum ProtocolType { Auto=0, Xml, XFlash };
    Q_ENUM(ProtocolType)

    explicit MediatekController(QObject* parent = nullptr);
    ~MediatekController() override;

    // Getters
    int deviceState() const { return m_deviceState; }
    QString portName() const { return m_portName; }
    bool isBusy() const { return m_busy; }
    bool isWatching() const { return m_watching; }
    int language() const { return m_language; }
    void setLanguage(int v) { if(m_language!=v){m_language=v; emit languageChanged(); emit readinessChanged();} }
    bool daReady() const { return m_daReady; }
    bool scatterReady() const { return m_scatterReady; }
    bool isDeviceReady() const { return m_deviceState >= Ready; }
    bool hasCheckedPartitions() const { return m_checkedCount > 0; }
    QString statusHint() const;
    QString daPath() const { return m_daPath; }
    QVariantList partitions() const { return m_partitions; }
    int firmwareEntryCount() const { return m_checkedCount; }
    QVariantMap deviceInfo() const { return m_deviceInfo; }
    QString chipName() const { return m_deviceInfo.value("chip","-").toString(); }
    QString hwCode() const { return m_deviceInfo.value("hwCode","-").toString(); }
    QString meId() const { return m_deviceInfo.value("meId","-").toString(); }
    QString socId() const { return m_deviceInfo.value("socId","-").toString(); }
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString speedText() const { return m_speedText; }
    QString etaText() const { return m_etaText; }
    QString elapsedText() const { return m_elapsedText; }
    int protocolType() const { return m_protocolType; }
    void setProtocolType(int type);

    // Actions
    Q_INVOKABLE void loadDaFile(const QString& path);
    Q_INVOKABLE void loadScatterFile(const QString& path);
    Q_INVOKABLE void loadFirmwareDir(const QString& dirPath);
    Q_INVOKABLE void startAutoDetect();
    Q_INVOKABLE void stopAutoDetect();
    Q_INVOKABLE void stopOperation();
    Q_INVOKABLE QStringList detectPorts();
    Q_INVOKABLE void disconnect();

    // Operations (need device ready)
    Q_INVOKABLE void readPartitionTable();
    Q_INVOKABLE void erasePartitions();
    Q_INVOKABLE void readFlash();
    Q_INVOKABLE void writeFlash();
    Q_INVOKABLE void formatAll();
    Q_INVOKABLE void readImei();
    Q_INVOKABLE void writeImei(const QString& imei);
    Q_INVOKABLE void readNvram();
    Q_INVOKABLE void unlockBootloader();
    Q_INVOKABLE void reboot();

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
    void protocolTypeChanged();
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

    int m_deviceState = Disconnected;
    int m_protocolType = Auto;
    int m_language = 0;
    QString m_portName;
    bool m_busy = false;
    bool m_watching = false;
    int m_watchTimerId = 0;

    std::unique_ptr<MediatekService> m_service;
    std::unique_ptr<ITransport> m_ownedTransport;  // Transport ownership

    bool m_daReady = false;
    bool m_scatterReady = false;
    QString m_daPath;
    QString m_scatterPath;

    QVariantList m_partitions;
    QVariantList m_allPartitions;
    int m_checkedCount = 0;
    QVariantMap m_deviceInfo;

    double m_progress = 0.0;
    QString m_progressText, m_speedText, m_etaText, m_elapsedText;
    qint64 m_progressStartMs=0, m_lastSpeedMs=0, m_lastSpeedBytes=0;
};

} // namespace sakura
