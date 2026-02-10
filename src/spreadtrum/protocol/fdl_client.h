#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <cstdint>

#include "common/partition_info.h"

namespace sakura {

class ITransport;

// ── BSL/FDL command set ─────────────────────────────────────────────────────

enum class BslCommand : uint16_t {
    CONNECT       = 0x0000,
    START_DATA    = 0x0001,
    MIDST_DATA    = 0x0002,
    END_DATA      = 0x0003,
    EXEC_DATA     = 0x0004,
    NORMAL_RESET  = 0x0005,
    READ_FLASH    = 0x0006,
    READ_CHIP_TYPE = 0x0007,
    ERASE_FLASH   = 0x000A,
    SET_BAUDRATE  = 0x000E,
    // Flash read flow: READ_START → READ_MIDST (with size+offset) → READ_END
    READ_START    = 0x0010,
    READ_MIDST    = 0x0011,
    READ_END      = 0x0012,
    // FDL2 extended commands
    READ_PARTITION     = 0x0024,
    READ_PARTITIONS    = 0x0025,
    WRITE_PARTITION    = 0x0026,
    ERASE_PARTITION    = 0x0027,
    REPARTITION        = 0x0028,
    READ_PMT           = 0x0029,
    POWER_OFF          = 0x002A,
    READ_IMEI          = 0x002B,
    WRITE_IMEI         = 0x002C,
    DISABLE_TRANSCODE  = 0x002D,
    ENABLE_WRITE_FLASH = 0x002E,
    CHANGE_BAUD_RATE   = 0x002F,
    READ_UID           = 0x0030,
    GET_VERSION        = 0x0031,
    LIST_PARTITIONS    = 0x0032,
    READ_EFUSE         = 0x0033,
    WRITE_EFUSE        = 0x0034,
};

// Response types from BSL/FDL
enum class BslResponse : uint16_t {
    ACK             = 0x0080,
    NAK             = 0x0000,
    INVALID_CMD     = 0x00FF,
    UNKNOWN_CMD     = 0x00FE,
    INVALID_ADDR    = 0x00FD,
    INVALID_BAUDRATE = 0x00FC,
    INVALID_PARTITION = 0x00FB,
    OPERATION_FAILED  = 0x00FA,
    REP_VER         = 0x0081,
    REP_DATA        = 0x0082,
    REP_READ_FLASH  = 0x0083,
};

// FDL loading stage
enum class FdlStage {
    None,
    FDL1,       // Running in BSL / BootROM
    FDL2        // Running in FDL1, loading FDL2
};

// ── FDL Client ──────────────────────────────────────────────────────────────

class FdlClient : public QObject {
    Q_OBJECT

public:
    explicit FdlClient(ITransport* transport, QObject* parent = nullptr);
    ~FdlClient() override;

    // Handshake / connection
    bool handshake();
    bool connect();

    // FDL download flow
    bool downloadFdl(const QByteArray& data, uint32_t addr, FdlStage stage);
    bool execData(uint32_t addr);

    // Transcoding control (must disable before binary transfers)
    bool disableTranscode();

    // Baud rate
    bool changeBaudRate(uint32_t baudRate);

    // Partition operations (FDL2 only)
    QList<PartitionInfo> readPartitions();
    bool writePartition(const QString& name, const QByteArray& data);
    QByteArray readPartition(const QString& name, qint64 offset = 0, qint64 length = -1);
    bool erasePartition(const QString& name);
    bool repartition(const QByteArray& partitionXml);

    // Device info
    QString getVersion();
    QByteArray readUid();
    QByteArray readImei();
    bool writeImei(const QByteArray& imei1, const QByteArray& imei2);

    // Control
    bool powerOff();
    bool normalReset();

    // State
    FdlStage currentStage() const { return m_stage; }
    void setStage(FdlStage stage) { m_stage = stage; }

signals:
    void transferProgress(qint64 current, qint64 total);

private:
    // Low-level HDLC packet I/O
    bool sendCommand(BslCommand cmd, const QByteArray& payload = {});
    QByteArray recvResponse(int timeoutMs = 5000);
    BslResponse parseResponseType(const QByteArray& resp) const;
    QByteArray parseResponseData(const QByteArray& resp) const;
    bool expectAck(int timeoutMs = 5000);

    // Chunked data transfer
    bool sendDataChunked(const QByteArray& data, uint32_t addr);

    ITransport* m_transport = nullptr;
    FdlStage m_stage = FdlStage::None;
    bool m_transcodeEnabled = true;

    static constexpr int HANDSHAKE_TIMEOUT = 3000;
    static constexpr int DEFAULT_TIMEOUT   = 5000;
    static constexpr int TRANSFER_TIMEOUT  = 30000;
    static constexpr int MAX_PACKET_SIZE   = 0x2000; // 8 KiB
};

} // namespace sakura
