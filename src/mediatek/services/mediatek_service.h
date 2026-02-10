#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <memory>

#include "common/partition_info.h"
#include "mediatek/protocol/brom_client.h"
#include "mediatek/protocol/da_loader.h"

namespace sakura {

class ITransport;
class XFlashClient;
class XmlDaClient;
class MtkSlaAuth;
class MtkChipDatabase;

// ── DA protocol selection ───────────────────────────────────────────────────

enum class MtkDaProtocol {
    Auto,       // Auto-detect based on DA response
    XFlash,     // Binary XFlash protocol
    XmlDa       // XML DA V6 protocol
};

// ── MediaTek service — orchestrates the full flash flow ─────────────────────

class MediatekService : public QObject {
    Q_OBJECT

public:
    explicit MediatekService(QObject* parent = nullptr);
    ~MediatekService() override;

    // Connection
    bool connectDevice(ITransport* transport);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // DA management
    bool loadDaFile(const QString& path);
    bool downloadDa();

    // Protocol selection
    void setProtocol(MtkDaProtocol protocol) { m_protocol = protocol; }
    MtkDaProtocol currentProtocol() const { return m_protocol; }

    // Partition operations (delegates to active DA client)
    QList<PartitionInfo> readPartitions();
    bool writePartition(const QString& name, const QByteArray& data);
    QByteArray readPartition(const QString& name, qint64 offset = 0, qint64 length = -1);
    bool erasePartition(const QString& name);
    bool formatAll();

    // Device info
    MtkDeviceInfo deviceInfo() const { return m_deviceInfo; }
    QString chipName() const;

    // Control
    bool reboot();
    bool shutdown();

signals:
    void stateChanged(int newState);
    void deviceInfoReady(const MtkDeviceInfo& info);
    void partitionsReady(const QList<PartitionInfo>& partitions);
    void transferProgress(qint64 current, qint64 total);
    void operationCompleted(bool success, const QString& message);
    void logMessage(const QString& message);

private:
    // Internal flow
    bool performHandshake();
    bool detectAndLoadDa();
    bool negotiateProtocol();
    bool handleSecureBoot();

    // State
    bool m_connected = false;
    MtkDaProtocol m_protocol = MtkDaProtocol::Auto;
    MtkDeviceInfo m_deviceInfo;

    // Owned objects
    ITransport* m_transport = nullptr;
    std::unique_ptr<BromClient> m_bromClient;
    std::unique_ptr<XFlashClient> m_xflashClient;
    std::unique_ptr<XmlDaClient> m_xmlDaClient;
    std::unique_ptr<MtkSlaAuth> m_slaAuth;
    DaLoader m_daLoader;
};

} // namespace sakura
