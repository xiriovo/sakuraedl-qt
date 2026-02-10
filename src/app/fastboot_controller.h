#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <memory>

namespace sakura {

class FastbootService;
class PayloadParser;

class FastbootController : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(bool isWatching READ isWatching NOTIFY connectionChanged)
    Q_PROPERTY(int language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString statusHint READ statusHint NOTIFY connectionChanged)

    // Device info
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString serialNo READ serialNo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString product READ product NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString currentSlot READ currentSlot NOTIFY deviceInfoChanged)
    Q_PROPERTY(bool isUnlocked READ isUnlocked NOTIFY deviceInfoChanged)
    Q_PROPERTY(int maxDownloadSize READ maxDownloadSize NOTIFY deviceInfoChanged)

    // Partitions / Images
    Q_PROPERTY(QVariantList partitions READ partitions NOTIFY partitionsChanged)
    Q_PROPERTY(bool hasCheckedPartitions READ hasCheckedPartitions NOTIFY partitionsChanged)
    Q_PROPERTY(bool payloadLoaded READ payloadLoaded NOTIFY payloadChanged)
    Q_PROPERTY(QString payloadPath READ payloadPath NOTIFY payloadChanged)

    // Progress
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY progressChanged)

public:
    explicit FastbootController(QObject* parent = nullptr);
    ~FastbootController() override;

    // Getters
    bool connected() const { return m_connected; }
    bool isBusy() const { return m_busy; }
    bool isWatching() const { return m_watching; }
    int language() const { return m_language; }
    void setLanguage(int v) { if(m_language!=v){m_language=v; emit languageChanged(); emit connectionChanged();} }
    QString statusHint() const;
    QVariantMap deviceInfo() const { return m_deviceInfo; }
    QString serialNo() const { return m_deviceInfo.value("serialno","-").toString(); }
    QString product() const { return m_deviceInfo.value("product","-").toString(); }
    QString currentSlot() const { return m_deviceInfo.value("current-slot","a").toString(); }
    bool isUnlocked() const { return m_deviceInfo.value("unlocked",false).toBool(); }
    int maxDownloadSize() const { return m_maxDownload; }
    QVariantList partitions() const { return m_partitions; }
    bool hasCheckedPartitions() const { return m_checkedCount > 0; }
    bool payloadLoaded() const { return m_payloadLoaded; }
    QString payloadPath() const { return m_payloadPath; }
    double progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString speedText() const { return m_speedText; }
    QString etaText() const { return m_etaText; }
    QString elapsedText() const { return m_elapsedText; }

    // Actions
    Q_INVOKABLE void startAutoDetect();
    Q_INVOKABLE void stopAutoDetect();
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void stopOperation();

    // File loading
    Q_INVOKABLE void loadImages(const QStringList& paths);
    Q_INVOKABLE void loadFirmwareDir(const QString& dirPath);
    Q_INVOKABLE void loadPayload(const QString& path);

    // Flash operations
    Q_INVOKABLE void flashAll();
    Q_INVOKABLE void flashPartition(const QString& name, const QString& filePath);
    Q_INVOKABLE void erasePartition(const QString& name);
    Q_INVOKABLE void eraseUserdata();

    // Device control
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void rebootBootloader();
    Q_INVOKABLE void rebootRecovery();
    Q_INVOKABLE void rebootFastbootd();

    // Unlock / Lock
    Q_INVOKABLE void unlockBootloader();
    Q_INVOKABLE void lockBootloader();

    // A/B Slots
    Q_INVOKABLE void setActiveSlot(const QString& slot);
    Q_INVOKABLE void getVariable(const QString& name);

    // OEM commands
    Q_INVOKABLE void oemCommand(const QString& cmd);

    // Partition table
    Q_INVOKABLE void readPartitionTable();

    // Custom command
    Q_INVOKABLE void customCommand(const QString& cmd);

    // Cloud URL parse
    Q_INVOKABLE void parseCloudUrl(const QString& url);

    // Payload
    Q_INVOKABLE void extractPayloadPartition(const QString& name, const QString& savePath);

    // Script
    Q_INVOKABLE void loadBatScript(const QString& path);
    Q_INVOKABLE void executeBatScript();

    // Partition management
    Q_INVOKABLE void togglePartition(int index);
    Q_INVOKABLE void selectAll(bool checked);

signals:
    void connectionChanged();
    void deviceInfoChanged();
    void partitionsChanged();
    void progressChanged();
    void busyChanged();
    void languageChanged();
    void payloadChanged();
    void operationCompleted(bool success, const QString& message);
    void variableResult(const QString& name, const QString& value);
    void logMessage(const QString& msg);

protected:
    void timerEvent(QTimerEvent* ev) override;

private:
    void setBusy(bool busy);
    void addLog(const QString& msg);
    void addLogOk(const QString& msg);
    void addLogErr(const QString& msg);
    void addLogFail(const QString& msg);
    bool zh() const { return m_language==0; }
    QString L(const char* z, const char* e) const;
    void updateProgress(qint64 cur, qint64 tot, const QString& label);
    void resetProgress();
    void doConnect(const QString& serial);
    void doRefreshInfo();

    bool m_connected = false;
    bool m_busy = false;
    bool m_watching = false;
    int m_watchTimerId = 0;
    int m_language = 0;

    std::unique_ptr<FastbootService> m_service;
    std::unique_ptr<PayloadParser>   m_payload;
    QVariantMap m_deviceInfo;
    int m_maxDownload = 0x20000000;

    QVariantList m_partitions;
    int m_checkedCount = 0;

    bool m_payloadLoaded = false;
    QString m_payloadPath;
    QStringList m_batScript;

    double m_progress = 0.0;
    QString m_progressText, m_speedText, m_etaText, m_elapsedText;
    qint64 m_progressStartMs=0, m_lastSpeedMs=0, m_lastSpeedBytes=0;
};

} // namespace sakura
