#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <cstdint>

#include "common/partition_info.h"

namespace sakura {

class ITransport;

// ── XFlash DA binary protocol ───────────────────────────────────────────────

namespace XFlashConst {
    constexpr uint32_t MAGIC = 0xFEEEEEEF;

    // Data types
    constexpr uint32_t DT_PROTOCOL_FLOW = 0x0001;
    constexpr uint32_t DT_MESSAGE       = 0x0002;

    // Commands (protocol flow)
    constexpr uint32_t CMD_READ_PARTITION   = 0x0001;
    constexpr uint32_t CMD_WRITE_PARTITION  = 0x0002;
    constexpr uint32_t CMD_ERASE_PARTITION  = 0x0003;
    constexpr uint32_t CMD_FORMAT_PARTITION = 0x0004;
    constexpr uint32_t CMD_GET_GPT          = 0x0005;
    constexpr uint32_t CMD_READ_FLASH       = 0x0006;
    constexpr uint32_t CMD_WRITE_FLASH      = 0x0007;
    constexpr uint32_t CMD_SHUTDOWN         = 0x000A;
    constexpr uint32_t CMD_REBOOT           = 0x000B;
    constexpr uint32_t CMD_GET_DA_INFO      = 0x0080;
    constexpr uint32_t CMD_GET_EMMC_INFO    = 0x0081;
    constexpr uint32_t CMD_GET_NAND_INFO    = 0x0082;
    constexpr uint32_t CMD_GET_UFS_INFO     = 0x0083;
    constexpr uint32_t CMD_SET_HOST_INFO    = 0x0084;
    constexpr uint32_t CMD_SET_BOOT_MODE    = 0x0085;

    // Status codes
    constexpr uint32_t STATUS_OK    = 0x0000;
    constexpr uint32_t STATUS_ERROR = 0xFFFF;
}

// ── XFlash packet layout: [magic(4)][dataType(4)][length(4)][command(4)][args...] ──

#pragma pack(push, 1)
struct XFlashPacketHeader {
    uint32_t magic    = XFlashConst::MAGIC;
    uint32_t dataType = 0;
    uint32_t length   = 0;   // payload length (after this header)
    uint32_t command  = 0;
};
#pragma pack(pop)

struct XFlashDaInfo {
    uint32_t daVersion = 0;
    uint32_t flashType = 0;   // 0=EMMC, 1=NAND, 2=UFS
    uint64_t flashSize = 0;
    QString  flashId;
};

// ── XFlash client ───────────────────────────────────────────────────────────

class XFlashClient : public QObject {
    Q_OBJECT

public:
    explicit XFlashClient(ITransport* transport, QObject* parent = nullptr);
    ~XFlashClient() override;

    // Partition operations
    QList<PartitionInfo> readPartitions();
    bool writePartition(const QString& name, const QByteArray& data);
    QByteArray readPartition(const QString& name, qint64 offset = 0, qint64 length = -1);
    bool erasePartition(const QString& name);
    bool formatPartition(const QString& name);

    // Flash-level operations
    QByteArray readFlash(uint64_t offset, uint64_t length);
    bool writeFlash(uint64_t offset, const QByteArray& data);

    // Device info
    XFlashDaInfo getDaInfo();

    // Control
    bool shutdown();
    bool reboot();

signals:
    void transferProgress(qint64 current, qint64 total);

private:
    QByteArray buildPacket(uint32_t dataType, uint32_t command,
                           const QByteArray& payload = {}) const;
    bool sendPacket(uint32_t dataType, uint32_t command,
                    const QByteArray& payload = {});
    XFlashPacketHeader recvHeader();
    QByteArray recvPayload(uint32_t length);
    bool checkStatus();

    ITransport* m_transport = nullptr;
    static constexpr int DEFAULT_TIMEOUT = 10000;
};

} // namespace sakura
