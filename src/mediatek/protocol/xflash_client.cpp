#include "xflash_client.h"
#include "transport/i_transport.h"
#include "common/gpt_parser.h"
#include "core/logger.h"

#include <QtEndian>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-XFLASH";

XFlashClient::XFlashClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

XFlashClient::~XFlashClient() = default;

// ── Partition operations ────────────────────────────────────────────────────

QList<PartitionInfo> XFlashClient::readPartitions()
{
    LOG_INFO_CAT(LOG_TAG, "Reading partition table via XFlash...");

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_GET_GPT))
        return {};

    XFlashPacketHeader hdr = recvHeader();
    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_ERROR_CAT(LOG_TAG, "Invalid response magic");
        return {};
    }

    QByteArray gptData = recvPayload(hdr.length);

    // Parse the raw GPT binary returned by DA
    QList<PartitionInfo> partitions;
    if (!gptData.isEmpty()) {
        auto result = GptParser::parse(gptData);
        if (result.success) {
            partitions = result.partitions;
            LOG_INFO_CAT(LOG_TAG, QString("Parsed %1 partitions from GPT").arg(partitions.size()));
        } else {
            LOG_ERROR_CAT(LOG_TAG, QString("GPT parse error: %1").arg(result.errorMessage));
        }
    }
    return partitions;
}

bool XFlashClient::writePartition(const QString& name, const QByteArray& data)
{
    LOG_INFO_CAT(LOG_TAG, QString("Writing partition '%1' (%2 bytes)").arg(name).arg(data.size()));

    // Build argument payload: partition name (UTF-8, null-terminated) + data length
    QByteArray nameBytes = name.toUtf8();
    nameBytes.append('\0');

    QByteArray args;
    args.reserve(nameBytes.size() + 8 + data.size());
    args.append(nameBytes);

    // Append 64-bit data length (little-endian)
    uint64_t dataLen = static_cast<uint64_t>(data.size());
    uint64_t leLen = qToLittleEndian(dataLen);
    args.append(reinterpret_cast<const char*>(&leLen), 8);

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_WRITE_PARTITION, args))
        return false;

    // Stream data in blocks
    constexpr int BLOCK_SIZE = 0x40000; // 256 KiB
    qint64 totalSent = 0;
    const qint64 totalSize = data.size();

    while (totalSent < totalSize) {
        int chunkLen = static_cast<int>(qMin<qint64>(BLOCK_SIZE, totalSize - totalSent));
        QByteArray chunk = data.mid(static_cast<int>(totalSent), chunkLen);
        qint64 written = m_transport->write(chunk);
        if (written != chunkLen) {
            LOG_ERROR_CAT(LOG_TAG, QString("Write failed at offset %1: wrote %2/%3")
                                       .arg(totalSent).arg(written).arg(chunkLen));
            return false;
        }
        totalSent += chunkLen;
        emit transferProgress(totalSent, totalSize);
    }

    return checkStatus();
}

QByteArray XFlashClient::readPartition(const QString& name, qint64 offset, qint64 length)
{
    LOG_INFO_CAT(LOG_TAG, QString("Reading partition '%1'").arg(name));

    QByteArray nameBytes = name.toUtf8();
    nameBytes.append('\0');

    QByteArray args;
    args.append(nameBytes);

    // Append offset and length (64-bit LE)
    uint64_t leOffset = qToLittleEndian(static_cast<uint64_t>(offset));
    uint64_t leLength = qToLittleEndian(static_cast<uint64_t>(length));
    args.append(reinterpret_cast<const char*>(&leOffset), 8);
    args.append(reinterpret_cast<const char*>(&leLength), 8);

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_READ_PARTITION, args))
        return {};

    // Receive data in chunks
    XFlashPacketHeader hdr = recvHeader();
    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_ERROR_CAT(LOG_TAG, "readPartition: invalid response magic");
        return {};
    }
    QByteArray result = recvPayload(hdr.length);

    if (!checkStatus()) {
        LOG_WARNING_CAT(LOG_TAG, "readPartition: status check failed after data");
    }
    return result;
}

bool XFlashClient::erasePartition(const QString& name)
{
    LOG_INFO_CAT(LOG_TAG, QString("Erasing partition '%1'").arg(name));

    QByteArray nameBytes = name.toUtf8();
    nameBytes.append('\0');

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_ERASE_PARTITION, nameBytes))
        return false;

    return checkStatus();
}

bool XFlashClient::formatPartition(const QString& name)
{
    LOG_INFO_CAT(LOG_TAG, QString("Formatting partition '%1'").arg(name));

    QByteArray nameBytes = name.toUtf8();
    nameBytes.append('\0');

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_FORMAT_PARTITION, nameBytes))
        return false;

    return checkStatus();
}

// ── Flash-level operations ──────────────────────────────────────────────────

QByteArray XFlashClient::readFlash(uint64_t offset, uint64_t length)
{
    QByteArray args;
    uint64_t leOff = qToLittleEndian(offset);
    uint64_t leLen = qToLittleEndian(length);
    args.append(reinterpret_cast<const char*>(&leOff), 8);
    args.append(reinterpret_cast<const char*>(&leLen), 8);

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_READ_FLASH, args))
        return {};

    XFlashPacketHeader hdr = recvHeader();
    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_ERROR_CAT(LOG_TAG, "readFlash: invalid response magic");
        return {};
    }
    return recvPayload(hdr.length);
}

bool XFlashClient::writeFlash(uint64_t offset, const QByteArray& data)
{
    QByteArray args;
    uint64_t leOff = qToLittleEndian(offset);
    uint64_t leLen = qToLittleEndian(static_cast<uint64_t>(data.size()));
    args.append(reinterpret_cast<const char*>(&leOff), 8);
    args.append(reinterpret_cast<const char*>(&leLen), 8);

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_WRITE_FLASH, args))
        return false;

    qint64 written = m_transport->write(data);
    if (written != data.size()) {
        LOG_ERROR_CAT(LOG_TAG, QString("writeFlash: wrote %1/%2 bytes")
                                   .arg(written).arg(data.size()));
        return false;
    }
    return checkStatus();
}

// ── Device info ─────────────────────────────────────────────────────────────

XFlashDaInfo XFlashClient::getDaInfo()
{
    XFlashDaInfo info;

    if (!sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_GET_DA_INFO))
        return info;

    XFlashPacketHeader hdr = recvHeader();
    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_ERROR_CAT(LOG_TAG, "getDaInfo: invalid response magic");
        return info;
    }
    QByteArray payload = recvPayload(hdr.length);

    if (payload.size() >= 16) {
        const auto* p = reinterpret_cast<const uint8_t*>(payload.constData());
        info.daVersion = qFromLittleEndian<uint32_t>(p);
        info.flashType = qFromLittleEndian<uint32_t>(p + 4);
        info.flashSize = qFromLittleEndian<uint64_t>(p + 8);
    }

    return info;
}

// ── Control ─────────────────────────────────────────────────────────────────

bool XFlashClient::shutdown()
{
    return sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_SHUTDOWN);
}

bool XFlashClient::reboot()
{
    return sendPacket(XFlashConst::DT_PROTOCOL_FLOW, XFlashConst::CMD_REBOOT);
}

// ── Private helpers ─────────────────────────────────────────────────────────

QByteArray XFlashClient::buildPacket(uint32_t dataType, uint32_t command,
                                      const QByteArray& payload) const
{
    XFlashPacketHeader hdr;
    hdr.magic    = qToLittleEndian(XFlashConst::MAGIC);
    hdr.dataType = qToLittleEndian(dataType);
    hdr.length   = qToLittleEndian(static_cast<uint32_t>(4 + payload.size()));
    hdr.command  = qToLittleEndian(command);

    QByteArray pkt(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    pkt.append(payload);
    return pkt;
}

bool XFlashClient::sendPacket(uint32_t dataType, uint32_t command,
                               const QByteArray& payload)
{
    QByteArray pkt = buildPacket(dataType, command, payload);
    return m_transport->write(pkt) == pkt.size();
}

XFlashPacketHeader XFlashClient::recvHeader()
{
    XFlashPacketHeader hdr{};
    QByteArray raw = m_transport->readExact(sizeof(XFlashPacketHeader), DEFAULT_TIMEOUT);

    if (raw.size() < static_cast<int>(sizeof(XFlashPacketHeader))) {
        LOG_ERROR_CAT(LOG_TAG, QString("recvHeader: short read (%1 bytes)").arg(raw.size()));
        return hdr;
    }

    const auto* p = reinterpret_cast<const XFlashPacketHeader*>(raw.constData());
    hdr.magic    = qFromLittleEndian(p->magic);
    hdr.dataType = qFromLittleEndian(p->dataType);
    hdr.length   = qFromLittleEndian(p->length);
    hdr.command  = qFromLittleEndian(p->command);

    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_WARNING_CAT(LOG_TAG, QString("recvHeader: bad magic 0x%1")
                                     .arg(hdr.magic, 8, 16, QChar('0')));
    }
    return hdr;
}

QByteArray XFlashClient::recvPayload(uint32_t length)
{
    if (length <= 4)
        return {};
    uint32_t payloadLen = length - 4; // Subtract the 4 bytes for the command field
    // Safety cap: reject absurdly large payloads that indicate corrupted packets
    constexpr uint32_t MAX_PAYLOAD = 256 * 1024 * 1024; // 256 MiB
    if (payloadLen > MAX_PAYLOAD) {
        LOG_ERROR_CAT(LOG_TAG, QString("recvPayload: length %1 exceeds safety limit").arg(payloadLen));
        return {};
    }
    return m_transport->readExact(static_cast<int>(payloadLen), DEFAULT_TIMEOUT);
}

bool XFlashClient::checkStatus()
{
    XFlashPacketHeader hdr = recvHeader();
    if (hdr.magic != XFlashConst::MAGIC) {
        LOG_ERROR_CAT(LOG_TAG, "Invalid status response magic");
        return false;
    }
    // Command field in status response carries the result code
    if (hdr.command != XFlashConst::STATUS_OK) {
        LOG_ERROR_CAT(LOG_TAG, QString("XFlash error: 0x%1").arg(hdr.command, 8, 16, QChar('0')));
        return false;
    }
    return true;
}

} // namespace sakura
