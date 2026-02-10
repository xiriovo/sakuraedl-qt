#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cstdint>
#include <functional>

#include "common/partition_info.h"

namespace sakura {

class ITransport;

// ─── Firehose storage types ──────────────────────────────────────────
enum class FirehoseStorageType {
    UFS,
    eMMC,
    NAND,
    SPINOR,
};

// ─── Firehose XML response ──────────────────────────────────────────
struct FirehoseResponse {
    bool success = false;
    QString rawValue;           // "ACK" / "NAK"
    QString logMessage;         // <log value="..."/>
    QByteArray rawXml;          // full raw XML
};

// ─── Firehose client ────────────────────────────────────────────────
class FirehoseClient : public QObject {
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    explicit FirehoseClient(ITransport* transport, QObject* parent = nullptr);

    // ── Configuration ────────────────────────────────────────────────
    bool configure(FirehoseStorageType storage = FirehoseStorageType::UFS,
                   uint32_t maxPayloadSize = 1048576,
                   bool skipStorageInit = false);

    void setMaxPayloadSize(uint32_t size) { m_maxPayloadSize = size; }
    uint32_t maxPayloadSize() const { return m_maxPayloadSize; }
    void setStorageType(FirehoseStorageType type) { m_storageType = type; }

    // ── Partition operations ─────────────────────────────────────────
    QList<PartitionInfo> readGptPartitions(uint32_t lun = 0);
    QByteArray readPartition(const QString& name, uint32_t lun = 0,
                             ProgressCallback progress = nullptr);
    bool writePartition(const QString& name, const QByteArray& data,
                        uint32_t lun = 0, ProgressCallback progress = nullptr);
    bool erasePartition(const QString& name, uint32_t lun = 0);

    // ── Device control ───────────────────────────────────────────────
    bool reset();
    bool powerOff();
    bool setActiveSlot(const QString& slot);
    bool setBootableStorageDrive(uint32_t lun);

    // ── Raw XML ──────────────────────────────────────────────────────
    FirehoseResponse sendRawXml(const QString& xml);
    bool ping();

    // ── Peek/Poke (memory access) ────────────────────────────────────
    QByteArray peekMemory(uint64_t address, uint32_t size);
    bool pokeMemory(uint64_t address, const QByteArray& data);

signals:
    void transferProgress(qint64 current, qint64 total);
    void logMessage(const QString& message);
    void statusMessage(const QString& message);

private:
    // ── XML building helpers ─────────────────────────────────────────
    QString buildConfigureXml(FirehoseStorageType storage, uint32_t payloadSize,
                              bool skipStorageInit);
    QString buildReadXml(uint64_t startSector, uint64_t numSectors,
                         uint32_t sectorSize, uint32_t lun);
    QString buildProgramXml(uint64_t startSector, uint64_t numSectors,
                            uint32_t sectorSize, uint32_t lun,
                            const QString& filename = QString());
    QString buildEraseXml(uint64_t startSector, uint64_t numSectors,
                          uint32_t sectorSize, uint32_t lun);
    QString buildPatchXml(uint64_t sectorOffset, uint32_t byteOffset,
                          uint32_t size, const QString& value, uint32_t lun);
    QString buildPowerXml(const QString& action);
    QString buildSetBootableXml(uint32_t lun);

    // ── Communication ────────────────────────────────────────────────
    bool sendXmlCommand(const QString& xml);
    FirehoseResponse receiveXmlResponse(int timeoutMs = 10000);
    FirehoseResponse parseResponse(const QByteArray& data);

    // ── Transfer helpers ─────────────────────────────────────────────
    bool writeDataChunked(const QByteArray& data, ProgressCallback progress);

    ITransport* m_transport = nullptr;
    FirehoseStorageType m_storageType = FirehoseStorageType::UFS;
    uint32_t m_maxPayloadSize = 1048576;  // 1 MB default
    uint32_t m_sectorSize = 512;

    static constexpr int XML_TIMEOUT_MS = 10000;
    static constexpr int DATA_TIMEOUT_MS = 60000;

    static QString storageTypeString(FirehoseStorageType type);
};

} // namespace sakura
