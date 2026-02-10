#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>

#include "common/partition_info.h"
#include "spreadtrum/protocol/fdl_client.h"

namespace sakura {

class ITransport;
class SprdDiagClient;
class PacParser;
class SprdFdlDatabase;

// ── Spreadtrum service — orchestrates the full flash flow ───────────────────

class SpreadtrumService : public QObject {
    Q_OBJECT

public:
    explicit SpreadtrumService(QObject* parent = nullptr);
    ~SpreadtrumService() override;

    // Connection
    bool connectDevice(ITransport* transport);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // FDL download flow
    bool loadFdl1(const QByteArray& data, uint32_t addr);
    bool loadFdl2(const QByteArray& data, uint32_t addr);
    bool loadFdl1FromDatabase(uint16_t chipId);
    bool loadFdl2FromDatabase(uint16_t chipId);

    // PAC firmware flash
    bool loadPacFile(const QString& path);
    bool flashPac();

    // Partition operations (FDL2 required)
    QList<PartitionInfo> readPartitions();
    bool writePartition(const QString& name, const QByteArray& data);
    QByteArray readPartition(const QString& name, qint64 offset = 0, qint64 length = -1);
    bool erasePartition(const QString& name);

    // Diag operations
    QByteArray readImei();
    bool writeImei(const QByteArray& imei1, const QByteArray& imei2);

    // Device info
    QString getVersion();
    FdlStage currentStage() const;

    // Control
    bool reboot();
    bool powerOff();

signals:
    void stateChanged(int newState);
    void partitionsReady(const QList<PartitionInfo>& partitions);
    void transferProgress(qint64 current, qint64 total);
    void operationCompleted(bool success, const QString& message);
    void logMessage(const QString& message);

private:
    bool performHandshake();
    bool enterFdl2();

    bool m_connected = false;
    ITransport* m_transport = nullptr;

    std::unique_ptr<FdlClient> m_fdlClient;
    std::unique_ptr<SprdDiagClient> m_diagClient;
    std::unique_ptr<PacParser> m_pacParser;
};

} // namespace sakura
