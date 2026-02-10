#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace sakura {

class QualcommService;
class ITransport;

class QualcommController : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(int connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY portChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool autoDetect READ autoDetect WRITE setAutoDetect NOTIFY optionsChanged)
    Q_PROPERTY(bool isWatching READ isWatching NOTIFY connectionStateChanged)
    Q_PROPERTY(QString detectStatus READ detectStatus NOTIFY connectionStateChanged)
    Q_PROPERTY(int language READ language WRITE setLanguage NOTIFY languageChanged)

    // Readiness & button states
    Q_PROPERTY(bool loaderReady READ loaderReady NOTIFY readinessChanged)
    Q_PROPERTY(bool xmlReady READ xmlReady NOTIFY readinessChanged)
    Q_PROPERTY(bool readyToFlash READ readyToFlash NOTIFY readinessChanged)
    Q_PROPERTY(bool hasCheckedPartitions READ hasCheckedPartitions NOTIFY partitionsChanged)
    Q_PROPERTY(bool isDeviceReady READ isDeviceReady NOTIFY connectionStateChanged)
    Q_PROPERTY(QString loaderPath READ loaderPath NOTIFY readinessChanged)
    Q_PROPERTY(QString statusHint READ statusHint NOTIFY readinessChanged)

    // Partitions
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY partitionsChanged)
    Q_PROPERTY(QVariantList patchEntries READ patchEntries NOTIFY partitionsChanged)
    Q_PROPERTY(int firmwareEntryCount READ firmwareEntryCount NOTIFY firmwareChanged)
    Q_PROPERTY(QString firmwarePath READ firmwarePath NOTIFY firmwareChanged)

    // Device info
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceBrand READ deviceBrand NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceModel READ deviceModel NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceChip READ deviceChip NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceSerial READ deviceSerial NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceStorage READ deviceStorage NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString deviceOta READ deviceOta NOTIFY deviceInfoChanged)

    // Progress
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY progressChanged)
    Q_PROPERTY(qint64 transferredBytes READ transferredBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)

    // Auth
    Q_PROPERTY(QString authMode READ authMode WRITE setAuthMode NOTIFY authModeChanged)
    Q_PROPERTY(QString storageType READ storageType WRITE setStorageType NOTIFY storageTypeChanged)
    Q_PROPERTY(QString vipDigestPath READ vipDigestPath WRITE setVipDigestPath NOTIFY authFilesChanged)
    Q_PROPERTY(QString vipSignPath READ vipSignPath WRITE setVipSignPath NOTIFY authFilesChanged)

    // Options
    Q_PROPERTY(bool skipSahara READ skipSahara WRITE setSkipSahara NOTIFY optionsChanged)
    Q_PROPERTY(bool autoReboot READ autoReboot WRITE setAutoReboot NOTIFY optionsChanged)
    Q_PROPERTY(bool protectPartitions READ protectPartitions WRITE setProtectPartitions NOTIFY optionsChanged)
    Q_PROPERTY(bool generateXml READ generateXml WRITE setGenerateXml NOTIFY optionsChanged)
    Q_PROPERTY(bool metaSuper READ metaSuper WRITE setMetaSuper NOTIFY optionsChanged)
    Q_PROPERTY(bool keepData READ keepData WRITE setKeepData NOTIFY optionsChanged)

public:
    enum ConnectionState { Disconnected=0, Connecting, SaharaMode, FirehoseMode, Ready, Error };
    Q_ENUM(ConnectionState)

    explicit QualcommController(QObject* parent = nullptr);
    ~QualcommController() override;

    // Getters
    int connectionState() const { return m_connectionState; }
    QString portName() const { return m_portName; }
    bool isBusy() const { return m_busy; }
    bool autoDetect() const { return m_autoDetect; }
    void setAutoDetect(bool v) { m_autoDetect=v; emit optionsChanged(); }
    bool isWatching() const { return m_watching; }
    QString detectStatus() const { return m_detectStatus; }
    int language() const { return m_language; }
    void setLanguage(int v) { if(m_language!=v){m_language=v; emit languageChanged(); emit readinessChanged();} }
    bool loaderReady() const { return m_loaderReady; }
    bool xmlReady() const { return m_xmlReady; }
    bool readyToFlash() const { return m_loaderReady && m_xmlReady; }
    bool hasCheckedPartitions() const { return m_firmwareEntryCount > 0; }
    bool isDeviceReady() const { return m_connectionState >= Ready; }
    QString loaderPath() const { return m_loaderPath; }
    QString statusHint() const;
    QVariantList partitions() const { return m_partitions; }
    QVariantList patchEntries() const { return m_patchEntries; }
    int firmwareEntryCount() const { return m_firmwareEntryCount; }
    QString firmwarePath() const { return m_firmwarePath; }
    QVariantMap deviceInfo() const { return m_deviceInfo; }
    QString deviceBrand() const { return m_deviceInfo.value("brand", "-").toString(); }
    QString deviceModel() const { return m_deviceInfo.value("model", "-").toString(); }
    QString deviceChip() const { return m_deviceInfo.value("chip", "-").toString(); }
    QString deviceSerial() const { return m_deviceInfo.value("serial", "-").toString(); }
    QString deviceStorage() const { return m_deviceInfo.value("storage", "-").toString(); }
    QString deviceOta() const { return m_deviceInfo.value("ota", "-").toString(); }
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString speedText() const { return m_speedText; }
    QString etaText() const { return m_etaText; }
    QString elapsedText() const { return m_elapsedText; }
    qint64 transferredBytes() const { return m_transferredBytes; }
    qint64 totalBytes() const { return m_totalBytes; }
    QString authMode() const { return m_authMode; }
    QString storageType() const { return m_storageType; }
    QString vipDigestPath() const { return m_vipDigestPath; }
    QString vipSignPath() const { return m_vipSignPath; }
    bool skipSahara() const { return m_skipSahara; }
    bool autoReboot() const { return m_autoReboot; }
    bool protectPartitions() const { return m_protectPartitions; }
    bool generateXml() const { return m_generateXml; }
    bool metaSuper() const { return m_metaSuper; }
    bool keepData() const { return m_keepData; }

    // Setters for options
    void setAuthMode(const QString& m) { if(m_authMode!=m){m_authMode=m; emit authModeChanged();} }
    void setStorageType(const QString& s) { if(m_storageType!=s){m_storageType=s; emit storageTypeChanged();} }
    void setVipDigestPath(const QString& p) { if(m_vipDigestPath!=p){m_vipDigestPath=p; emit authFilesChanged();} }
    void setVipSignPath(const QString& p) { if(m_vipSignPath!=p){m_vipSignPath=p; emit authFilesChanged();} }
    void setSkipSahara(bool v) { m_skipSahara=v; emit optionsChanged(); }
    void setAutoReboot(bool v) { m_autoReboot=v; emit optionsChanged(); }
    void setProtectPartitions(bool v) { m_protectPartitions=v; emit optionsChanged(); }
    void setGenerateXml(bool v) { m_generateXml=v; emit optionsChanged(); }
    void setMetaSuper(bool v) { m_metaSuper=v; emit optionsChanged(); }
    void setKeepData(bool v) { m_keepData=v; emit optionsChanged(); }

    // ── Actions (QML-invokable) ──

    // Connection
    Q_INVOKABLE void connectDevice(const QString& port = QString());
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void stopOperation();
    Q_INVOKABLE void startAutoDetect();
    Q_INVOKABLE void stopAutoDetect();
    Q_INVOKABLE QStringList detectPorts();

protected:
    void timerEvent(QTimerEvent* ev) override;

    // Partition operations
    Q_INVOKABLE void readPartitionTable();
    Q_INVOKABLE void readPartitions();     // read checked partitions to file
    Q_INVOKABLE void writePartitions();    // write files to checked partitions
    Q_INVOKABLE void erasePartitions();    // erase checked partitions

    // Device control
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void rebootToEdl();
    Q_INVOKABLE void switchSlot(const QString& slot);
    Q_INVOKABLE void setBootLun(int lun);

    // Auth (legacy — now auto-executed during connect)
    Q_INVOKABLE void performVipAuth(const QString& digestPath, const QString& signPath);

    // File loading
    Q_INVOKABLE void loadMultipleFiles(const QStringList& paths);
    Q_INVOKABLE void loadGptBin(const QString& path);
    Q_INVOKABLE void loadFirmwareDir(const QString& dirPath);
    Q_INVOKABLE void loadLoader(const QString& path);
    Q_INVOKABLE void autoMatchLoader();

    // Partition table management
    Q_INVOKABLE void togglePartition(int index);
    Q_INVOKABLE void selectAll(bool checked);
    Q_INVOKABLE void searchPartition(const QString& keyword);

    // Flash
    Q_INVOKABLE void flashFirmwarePackage();
    Q_INVOKABLE void testProgress();

    // Assign image files to checked partitions
    Q_INVOKABLE void assignImageFiles(const QStringList& filePaths);

signals:
    void connectionStateChanged();
    void portChanged();
    void busyChanged();
    void partitionsChanged();
    void firmwareChanged();
    void deviceInfoChanged();
    void progressChanged();
    void authModeChanged();
    void storageTypeChanged();
    void authFilesChanged();
    void optionsChanged();
    void operationCompleted(bool success, const QString& message);
    void logMessage(const QString& msg);
    void xiaomiAuthTokenRequired(const QString& token);
    void languageChanged();
    void readinessChanged();

private:
    void setConnectionState(int state);
    void setProgress(double value, const QString& text);
    void updateProgress(qint64 current, qint64 total, const QString& label);
    void resetProgress();
    void setBusy(bool busy);
    void addLog(const QString& msg);
    void addLogOk(const QString& msg);   // [OKAY] green
    void addLogErr(const QString& msg);  // [ERROR] red
    void addLogFail(const QString& msg); // [FAIL] yellow
    bool curLangZh() const;
    QString L(const char* zh, const char* en) const;
    void tryStartAutoDetect();
    void doReadPartitionTable();
    void setupAuthStrategy();  // Create & set auth strategy based on m_authMode

    // ── Unified "ensure connected then operate" mechanism ──
    enum PendingOp {
        NoPending = 0,
        PendingReadGpt,
        PendingReadPartitions,
        PendingWritePartitions,
        PendingErasePartitions,
        PendingReboot,
        PendingPowerOff,
    };
    void ensureConnectedThen(PendingOp op);
    void executePendingOp();
    void cancelPendingOp(const QString& reason);

    // Service
    std::unique_ptr<QualcommService> m_service;

    // Transport ownership — must outlive the service connection
    std::unique_ptr<ITransport> m_ownedTransport;

    // Connection
    int m_connectionState = Disconnected;
    QString m_portName;
    bool m_busy = false;
    bool m_autoDetect = true;
    bool m_watching = false;
    QString m_detectStatus;
    int m_watchTimerId = 0;
    int m_language = 0; // 0=zh, 1=en

    // Readiness
    bool m_loaderReady = false;
    bool m_xmlReady = false;
    QString m_loaderPath;
    PendingOp m_pendingOp = NoPending; // operation to execute after auto-connect

    // Partitions
    QVariantList m_partitions;
    QVariantList m_allPartitions; // unfiltered
    QVariantList m_patchEntries;
    int m_firmwareEntryCount = 0;
    QString m_firmwarePath;
    QString m_firmwareDir;

    // Device
    QVariantMap m_deviceInfo;

    // Progress
    double m_progress = 0.0;
    QString m_progressText, m_speedText, m_etaText, m_elapsedText;
    qint64 m_transferredBytes = 0, m_totalBytes = 0;
    qint64 m_progressStartMs = 0, m_lastSpeedBytes = 0, m_lastSpeedMs = 0;

    // Auth / Options
    QString m_authMode = "none";  // none, demacia, vip, xiaomi
    QString m_storageType = "ufs"; // ufs, emmc
    QString m_vipDigestPath;
    QString m_vipSignPath;
    bool m_skipSahara = false;
    bool m_autoReboot = false;
    bool m_protectPartitions = true;
    bool m_generateXml = false;
    bool m_metaSuper = false;
    bool m_keepData = false;
};

} // namespace sakura
