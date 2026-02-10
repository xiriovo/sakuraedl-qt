#include "fdl_client.h"
#include "spreadtrum/protocol/hdlc_protocol.h"
#include "transport/i_transport.h"
#include "core/logger.h"

#include <QThread>
#include <QtEndian>

namespace sakura {

static constexpr char LOG_TAG[] = "SPRD-FDL";

FdlClient::FdlClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

FdlClient::~FdlClient() = default;

// ── Handshake ───────────────────────────────────────────────────────────────

bool FdlClient::handshake()
{
    LOG_INFO_CAT(LOG_TAG, "Starting FDL handshake...");

    // Spreadtrum handshake: send 0x7E repeatedly until we get 0x7E back
    constexpr uint8_t SYNC_BYTE = 0x7E;
    constexpr int MAX_ATTEMPTS = 100;

    for (int i = 0; i < MAX_ATTEMPTS; ++i) {
        QByteArray syncPkt(1, static_cast<char>(SYNC_BYTE));
        m_transport->write(syncPkt);

        QByteArray resp = m_transport->read(1, 50);
        if (!resp.isEmpty() && static_cast<uint8_t>(resp[0]) == SYNC_BYTE) {
            LOG_INFO_CAT(LOG_TAG, QString("Handshake sync after %1 attempts").arg(i + 1));
            return true;
        }

        QThread::msleep(20);
    }

    LOG_ERROR_CAT(LOG_TAG, "Handshake failed — no sync response");
    return false;
}

bool FdlClient::connect()
{
    LOG_INFO_CAT(LOG_TAG, "Sending CONNECT...");
    if (!sendCommand(BslCommand::CONNECT))
        return false;
    return expectAck(HANDSHAKE_TIMEOUT);
}

// ── FDL download ────────────────────────────────────────────────────────────

bool FdlClient::downloadFdl(const QByteArray& data, uint32_t addr, FdlStage stage)
{
    LOG_INFO_CAT(LOG_TAG, QString("Downloading %1 (%2 bytes) to 0x%3")
                              .arg(stage == FdlStage::FDL1 ? "FDL1" : "FDL2")
                              .arg(data.size())
                              .arg(addr, 8, 16, QChar('0')));

    // Step 1: START_DATA with address and length
    QByteArray startPayload;
    uint32_t beAddr = qToBigEndian(addr);
    uint32_t beSize = qToBigEndian(static_cast<uint32_t>(data.size()));
    startPayload.append(reinterpret_cast<const char*>(&beAddr), 4);
    startPayload.append(reinterpret_cast<const char*>(&beSize), 4);

    if (!sendCommand(BslCommand::START_DATA, startPayload))
        return false;

    if (!expectAck()) {
        LOG_ERROR_CAT(LOG_TAG, "START_DATA not acknowledged");
        return false;
    }

    // Step 2: MIDST_DATA chunks
    if (!sendDataChunked(data, addr))
        return false;

    // Step 3: END_DATA
    if (!sendCommand(BslCommand::END_DATA))
        return false;

    if (!expectAck()) {
        LOG_ERROR_CAT(LOG_TAG, "END_DATA not acknowledged");
        return false;
    }

    m_stage = stage;
    LOG_INFO_CAT(LOG_TAG, QString("%1 downloaded successfully")
                              .arg(stage == FdlStage::FDL1 ? "FDL1" : "FDL2"));
    return true;
}

bool FdlClient::execData(uint32_t addr)
{
    LOG_INFO_CAT(LOG_TAG, QString("Executing at 0x%1").arg(addr, 8, 16, QChar('0')));

    QByteArray payload;
    uint32_t beAddr = qToBigEndian(addr);
    payload.append(reinterpret_cast<const char*>(&beAddr), 4);

    if (!sendCommand(BslCommand::EXEC_DATA, payload))
        return false;

    return expectAck(TRANSFER_TIMEOUT);
}

// ── Transcoding control ─────────────────────────────────────────────────────

bool FdlClient::disableTranscode()
{
    LOG_INFO_CAT(LOG_TAG, "Disabling HDLC transcoding...");

    if (!sendCommand(BslCommand::DISABLE_TRANSCODE))
        return false;

    if (!expectAck()) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to disable transcoding");
        return false;
    }

    m_transcodeEnabled = false;
    LOG_INFO_CAT(LOG_TAG, "Transcoding disabled");
    return true;
}

bool FdlClient::changeBaudRate(uint32_t baudRate)
{
    QByteArray payload;
    uint32_t beBaud = qToBigEndian(baudRate);
    payload.append(reinterpret_cast<const char*>(&beBaud), 4);

    if (!sendCommand(BslCommand::SET_BAUDRATE, payload))
        return false;

    return expectAck();
}

// ── Partition operations ────────────────────────────────────────────────────

QList<PartitionInfo> FdlClient::readPartitions()
{
    LOG_INFO_CAT(LOG_TAG, "Reading partition table...");

    if (!sendCommand(BslCommand::LIST_PARTITIONS))
        return {};

    QByteArray resp = recvResponse(DEFAULT_TIMEOUT);
    BslResponse type = parseResponseType(resp);

    if (type != BslResponse::REP_DATA) {
        LOG_ERROR_CAT(LOG_TAG, "Unexpected response to LIST_PARTITIONS");
        return {};
    }

    QByteArray data = parseResponseData(resp);

    // Parse the partition table from binary response
    // Format: [count(4)] [entry[name(72), offset(8), size(8)]]...
    QList<PartitionInfo> partitions;

    if (data.size() < 4)
        return partitions;

    const auto* p = reinterpret_cast<const uint8_t*>(data.constData());
    uint32_t count = qFromBigEndian<uint32_t>(p);
    int offset = 4;

    for (uint32_t i = 0; i < count && offset + 88 <= data.size(); ++i) {
        PartitionInfo part;
        part.name = QString::fromUtf8(data.mid(offset, 72)).trimmed();
        part.startSector = qFromBigEndian<uint64_t>(
            reinterpret_cast<const uint8_t*>(data.constData() + offset + 72));
        part.sizeBytes = qFromBigEndian<uint64_t>(
            reinterpret_cast<const uint8_t*>(data.constData() + offset + 80));
        part.numSectors = part.sizeBytes / 512;

        partitions.append(part);
        offset += 88;
    }

    LOG_INFO_CAT(LOG_TAG, QString("Found %1 partitions").arg(partitions.size()));
    return partitions;
}

bool FdlClient::writePartition(const QString& name, const QByteArray& data)
{
    LOG_INFO_CAT(LOG_TAG, QString("Writing partition '%1' (%2 bytes)").arg(name).arg(data.size()));

    // Encode partition name + data size as START_DATA payload
    QByteArray nameBytes = name.toUtf8();
    nameBytes.resize(72, '\0'); // Pad to fixed width

    QByteArray startPayload;
    startPayload.append(nameBytes);
    uint32_t beSize = qToBigEndian(static_cast<uint32_t>(data.size()));
    startPayload.append(reinterpret_cast<const char*>(&beSize), 4);

    if (!sendCommand(BslCommand::WRITE_PARTITION, startPayload))
        return false;

    if (!expectAck()) {
        LOG_ERROR_CAT(LOG_TAG, "WRITE_PARTITION not acknowledged");
        return false;
    }

    // Send data in chunks
    if (!sendDataChunked(data, 0))
        return false;

    // End
    if (!sendCommand(BslCommand::END_DATA))
        return false;

    return expectAck(TRANSFER_TIMEOUT);
}

QByteArray FdlClient::readPartition(const QString& name, qint64 offset, qint64 length)
{
    LOG_INFO_CAT(LOG_TAG, QString("Reading partition '%1' offset=%2 length=%3")
                              .arg(name).arg(offset).arg(length));

    // Step 1: READ_START — tell device which partition to read
    QByteArray nameBytes = name.toUtf8();
    nameBytes.resize(72, '\0');

    QByteArray startPayload;
    startPayload.append(nameBytes);
    // Total size to read (little-endian, per edl2/spd_dump reference)
    uint32_t leLen = qToLittleEndian(static_cast<uint32_t>(length > 0 ? length : 0));
    startPayload.append(reinterpret_cast<const char*>(&leLen), 4);

    if (!sendCommand(BslCommand::READ_START, startPayload)) {
        LOG_ERROR_CAT(LOG_TAG, "READ_START failed");
        return {};
    }

    if (!expectAck(DEFAULT_TIMEOUT)) {
        LOG_ERROR_CAT(LOG_TAG, "No ACK for READ_START");
        return {};
    }

    // Step 2: READ_MIDST in a loop — request data chunks
    QByteArray result;
    constexpr int CHUNK_SIZE = 4096;
    qint64 currentOffset = offset;
    qint64 remaining = length;

    while (remaining > 0 || length <= 0) {
        int nowRead = (length > 0) ? static_cast<int>(qMin<qint64>(CHUNK_SIZE, remaining)) : CHUNK_SIZE;

        // Build READ_MIDST payload: [read_size LE 4] + [offset LE 4]
        QByteArray midstPayload;
        uint32_t leReadSize = qToLittleEndian(static_cast<uint32_t>(nowRead));
        uint32_t leOffset = qToLittleEndian(static_cast<uint32_t>(currentOffset));
        midstPayload.append(reinterpret_cast<const char*>(&leReadSize), 4);
        midstPayload.append(reinterpret_cast<const char*>(&leOffset), 4);

        if (!sendCommand(BslCommand::READ_MIDST, midstPayload))
            break;

        // Receive data response
        QByteArray resp = recvResponse(TRANSFER_TIMEOUT);
        BslResponse type = parseResponseType(resp);

        if (type == BslResponse::REP_DATA || type == BslResponse::REP_READ_FLASH) {
            QByteArray chunk = parseResponseData(resp);
            if (chunk.isEmpty()) break;
            result.append(chunk);
            currentOffset += chunk.size();
            if (length > 0) {
                remaining -= chunk.size();
                emit transferProgress(result.size(), length);
            }
        } else if (type == BslResponse::ACK) {
            break;  // Transfer complete
        } else {
            LOG_ERROR_CAT(LOG_TAG, QString("Unexpected response during read: 0x%1")
                                       .arg(static_cast<uint16_t>(type), 4, 16, QChar('0')));
            break;
        }

        if (length <= 0 && result.size() >= 512 * 1024 * 1024)
            break;  // Safety limit for unknown-length reads
    }

    // Step 3: READ_END — signal completion
    sendCommand(BslCommand::READ_END);
    expectAck(DEFAULT_TIMEOUT);

    LOG_INFO_CAT(LOG_TAG, QString("Read complete: %1 bytes").arg(result.size()));
    return result;
}

bool FdlClient::erasePartition(const QString& name)
{
    LOG_INFO_CAT(LOG_TAG, QString("Erasing partition '%1'").arg(name));

    QByteArray nameBytes = name.toUtf8();
    nameBytes.resize(72, '\0');

    if (!sendCommand(BslCommand::ERASE_PARTITION, nameBytes))
        return false;

    return expectAck(TRANSFER_TIMEOUT);
}

bool FdlClient::repartition(const QByteArray& partitionXml)
{
    LOG_INFO_CAT(LOG_TAG, "Repartitioning...");

    if (!sendCommand(BslCommand::REPARTITION, partitionXml))
        return false;

    return expectAck(TRANSFER_TIMEOUT);
}

// ── Device info ─────────────────────────────────────────────────────────────

QString FdlClient::getVersion()
{
    if (!sendCommand(BslCommand::GET_VERSION))
        return {};

    QByteArray resp = recvResponse(DEFAULT_TIMEOUT);
    BslResponse type = parseResponseType(resp);

    if (type == BslResponse::REP_VER)
        return QString::fromUtf8(parseResponseData(resp));

    return {};
}

QByteArray FdlClient::readUid()
{
    if (!sendCommand(BslCommand::READ_UID))
        return {};

    QByteArray resp = recvResponse(DEFAULT_TIMEOUT);
    if (parseResponseType(resp) == BslResponse::REP_DATA)
        return parseResponseData(resp);

    return {};
}

QByteArray FdlClient::readImei()
{
    if (!sendCommand(BslCommand::READ_IMEI))
        return {};

    QByteArray resp = recvResponse(DEFAULT_TIMEOUT);
    if (parseResponseType(resp) == BslResponse::REP_DATA)
        return parseResponseData(resp);

    return {};
}

bool FdlClient::writeImei(const QByteArray& imei1, const QByteArray& imei2)
{
    QByteArray payload;
    payload.append(imei1);
    payload.append(imei2);

    if (!sendCommand(BslCommand::WRITE_IMEI, payload))
        return false;

    return expectAck();
}

// ── Control ─────────────────────────────────────────────────────────────────

bool FdlClient::powerOff()
{
    if (!sendCommand(BslCommand::POWER_OFF))
        return false;
    return expectAck();
}

bool FdlClient::normalReset()
{
    if (!sendCommand(BslCommand::NORMAL_RESET))
        return false;
    return expectAck();
}

// ── Private helpers ─────────────────────────────────────────────────────────

bool FdlClient::sendCommand(BslCommand cmd, const QByteArray& payload)
{
    uint16_t cmdVal = static_cast<uint16_t>(cmd);
    QByteArray pkt = SprdHdlcProtocol::encode(cmdVal, payload, m_transcodeEnabled);
    return m_transport->write(pkt) == pkt.size();
}

QByteArray FdlClient::recvResponse(int timeoutMs)
{
    // Read until we get a complete HDLC frame
    QByteArray raw = m_transport->read(MAX_PACKET_SIZE + 64, timeoutMs);
    if (raw.isEmpty())
        return {};

    QByteArray decoded = SprdHdlcProtocol::decode(raw, m_transcodeEnabled);
    if (decoded.isEmpty()) {
        LOG_WARNING_CAT(LOG_TAG, "Failed to decode HDLC response");
    }
    return decoded;
}

BslResponse FdlClient::parseResponseType(const QByteArray& resp) const
{
    if (resp.size() < 2)
        return BslResponse::NAK;

    uint16_t type = qFromBigEndian<uint16_t>(
        reinterpret_cast<const uchar*>(resp.constData()));
    return static_cast<BslResponse>(type);
}

QByteArray FdlClient::parseResponseData(const QByteArray& resp) const
{
    if (resp.size() <= 2)
        return {};
    return resp.mid(2);
}

bool FdlClient::expectAck(int timeoutMs)
{
    QByteArray resp = recvResponse(timeoutMs);
    BslResponse type = parseResponseType(resp);

    if (type != BslResponse::ACK) {
        LOG_ERROR_CAT(LOG_TAG, QString("Expected ACK, got 0x%1")
                                   .arg(static_cast<uint16_t>(type), 4, 16, QChar('0')));
        return false;
    }
    return true;
}

bool FdlClient::sendDataChunked(const QByteArray& data, uint32_t /*addr*/)
{
    // addr is already sent via START_DATA; MIDST_DATA carries only payload

    qint64 totalSent = 0;
    const qint64 totalSize = data.size();
    const qint64 maxChunk = (MAX_PACKET_SIZE > 16) ? (MAX_PACKET_SIZE - 16) : 1;

    while (totalSent < totalSize) {
        int chunkLen = static_cast<int>(qMin<qint64>(maxChunk, totalSize - totalSent));
        QByteArray chunk = data.mid(static_cast<int>(totalSent), chunkLen);

        if (!sendCommand(BslCommand::MIDST_DATA, chunk)) {
            LOG_ERROR_CAT(LOG_TAG, "Failed to send data chunk");
            return false;
        }

        if (!expectAck()) {
            LOG_ERROR_CAT(LOG_TAG, "Data chunk not acknowledged");
            return false;
        }

        totalSent += chunkLen;
        emit transferProgress(totalSent, totalSize);
    }

    return true;
}

} // namespace sakura
