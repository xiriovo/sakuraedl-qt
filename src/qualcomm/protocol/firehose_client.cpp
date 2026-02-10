#include "firehose_client.h"
#include "transport/i_transport.h"
#include "core/logger.h"
#include "common/gpt_parser.h"

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cstring>

static const QString TAG = QStringLiteral("Firehose");

namespace sakura {

FirehoseClient::FirehoseClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

// ─── Storage type to string ──────────────────────────────────────────

QString FirehoseClient::storageTypeString(FirehoseStorageType type)
{
    switch (type) {
    case FirehoseStorageType::UFS:    return QStringLiteral("UFS");
    case FirehoseStorageType::eMMC:   return QStringLiteral("emmc");
    case FirehoseStorageType::NAND:   return QStringLiteral("nand");
    case FirehoseStorageType::SPINOR: return QStringLiteral("spinor");
    }
    return QStringLiteral("UFS");
}

// ─── XML building helpers ────────────────────────────────────────────

QString FirehoseClient::buildConfigureXml(FirehoseStorageType storage,
                                           uint32_t payloadSize,
                                           bool skipStorageInit)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("configure");
    w.writeAttribute("MemoryName", storageTypeString(storage));
    w.writeAttribute("MaxPayloadSizeToTargetInBytes", QString::number(payloadSize));
    w.writeAttribute("verbose", "0");
    w.writeAttribute("ZlpAwareHost", "1");
    w.writeAttribute("SkipStorageInit", skipStorageInit ? "1" : "0");
    w.writeEndElement(); // configure
    w.writeEndElement(); // data
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildReadXml(uint64_t startSector, uint64_t numSectors,
                                      uint32_t sectorSize, uint32_t lun)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("read");
    w.writeAttribute("SECTOR_SIZE_IN_BYTES", QString::number(sectorSize));
    w.writeAttribute("num_partition_sectors", QString::number(numSectors));
    w.writeAttribute("physical_partition_number", QString::number(lun));
    w.writeAttribute("start_sector", QString::number(startSector));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildProgramXml(uint64_t startSector, uint64_t numSectors,
                                         uint32_t sectorSize, uint32_t lun,
                                         const QString& filename)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("program");
    w.writeAttribute("SECTOR_SIZE_IN_BYTES", QString::number(sectorSize));
    w.writeAttribute("num_partition_sectors", QString::number(numSectors));
    w.writeAttribute("physical_partition_number", QString::number(lun));
    w.writeAttribute("start_sector", QString::number(startSector));
    if (!filename.isEmpty())
        w.writeAttribute("filename", filename);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildEraseXml(uint64_t startSector, uint64_t numSectors,
                                       uint32_t sectorSize, uint32_t lun)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("erase");
    w.writeAttribute("SECTOR_SIZE_IN_BYTES", QString::number(sectorSize));
    w.writeAttribute("num_partition_sectors", QString::number(numSectors));
    w.writeAttribute("physical_partition_number", QString::number(lun));
    w.writeAttribute("start_sector", QString::number(startSector));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildPatchXml(uint64_t sectorOffset, uint32_t byteOffset,
                                       uint32_t size, const QString& value, uint32_t lun)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("patch");
    w.writeAttribute("SECTOR_SIZE_IN_BYTES", QString::number(m_sectorSize));
    w.writeAttribute("byte_offset", QString::number(byteOffset));
    w.writeAttribute("filename", "DISK");
    w.writeAttribute("physical_partition_number", QString::number(lun));
    w.writeAttribute("sector_offset", QString::number(sectorOffset));
    w.writeAttribute("size_in_bytes", QString::number(size));
    w.writeAttribute("value", value);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildPowerXml(const QString& action)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("power");
    w.writeAttribute("value", action);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

QString FirehoseClient::buildSetBootableXml(uint32_t lun)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("setbootablestoragedrive");
    w.writeAttribute("value", QString::number(lun));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return xml;
}

// ─── Communication ───────────────────────────────────────────────────

bool FirehoseClient::sendXmlCommand(const QString& xml)
{
    QByteArray data = xml.toUtf8();

    // Firehose expects XML padded to sector-aligned size or max payload
    // Pad to the next sector boundary if needed
    if (m_sectorSize > 0) {
        int padded = data.size();
        if (padded % m_sectorSize != 0) {
            padded = ((padded / m_sectorSize) + 1) * m_sectorSize;
        }
        data.resize(padded, '\0');
    }

    qint64 written = m_transport->write(data);
    if (written != data.size()) {
        LOG_ERROR_CAT(TAG, "Failed to send XML command");
        return false;
    }
    return true;
}

FirehoseResponse FirehoseClient::receiveXmlResponse(int timeoutMs)
{
    QByteArray accumulated;
    int elapsed = 0;
    const int pollInterval = 100;
    constexpr int MAX_ACCUMULATE = 16 * 1024 * 1024; // 16 MB safety cap

    while (elapsed < timeoutMs) {
        QByteArray chunk = m_transport->read(m_maxPayloadSize, pollInterval);
        if (!chunk.isEmpty()) {
            if (accumulated.size() + chunk.size() > MAX_ACCUMULATE) {
                LOG_ERROR_CAT(TAG, "XML response exceeds safety limit");
                break;
            }
            accumulated.append(chunk);

            // Try to parse once we have a complete XML response
            FirehoseResponse resp = parseResponse(accumulated);
            if (resp.success || !resp.rawValue.isEmpty()) {
                return resp;
            }

            // Check for log messages and emit them
            if (accumulated.contains("<log ")) {
                QXmlStreamReader reader(accumulated);
                while (!reader.atEnd()) {
                    reader.readNext();
                    if (reader.isStartElement() && reader.name() == QStringLiteral("log")) {
                        QString logVal = reader.attributes().value("value").toString();
                        if (!logVal.isEmpty()) {
                            LOG_DEBUG_CAT(TAG, QString("[Device] %1").arg(logVal));
                            emit logMessage(logVal);
                        }
                    }
                }
            }
        }
        elapsed += pollInterval;
    }

    // Timeout — return whatever we have
    if (!accumulated.isEmpty()) {
        return parseResponse(accumulated);
    }

    FirehoseResponse timeout;
    timeout.success = false;
    timeout.rawValue = "TIMEOUT";
    return timeout;
}

FirehoseResponse FirehoseClient::parseResponse(const QByteArray& data)
{
    FirehoseResponse result;
    result.rawXml = data;

    // Strip null bytes from end for parsing
    QByteArray clean = data;
    while (clean.endsWith('\0'))
        clean.chop(1);

    if (clean.isEmpty())
        return result;

    QXmlStreamReader reader(clean);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("response")) {
                result.rawValue = reader.attributes().value("value").toString();
                result.success = (result.rawValue.compare("ACK", Qt::CaseInsensitive) == 0);
            } else if (reader.name() == QStringLiteral("log")) {
                result.logMessage = reader.attributes().value("value").toString();
            }
        }
    }

    return result;
}

// ─── Configure ───────────────────────────────────────────────────────

bool FirehoseClient::configure(FirehoseStorageType storage,
                                uint32_t maxPayloadSize,
                                bool skipStorageInit)
{
    LOG_INFO_CAT(TAG, QString("Configuring: storage=%1, payload=%2")
                    .arg(storageTypeString(storage)).arg(maxPayloadSize));

    m_storageType = storage;
    m_maxPayloadSize = maxPayloadSize;

    // Set sector size based on storage type
    m_sectorSize = (storage == FirehoseStorageType::UFS) ? 4096 : 512;

    QString xml = buildConfigureXml(storage, maxPayloadSize, skipStorageInit);
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    if (!resp.success) {
        LOG_ERROR_CAT(TAG, QString("Configure failed: %1").arg(resp.rawValue));

        // Some loaders respond with a lower MaxPayloadSizeToTargetInBytes
        // Try to parse the response for a counter-offer
        if (!resp.rawXml.isEmpty()) {
            QXmlStreamReader reader(resp.rawXml);
            while (!reader.atEnd()) {
                reader.readNext();
                if (reader.isStartElement() && reader.name() == QStringLiteral("response")) {
                    QString newSize = reader.attributes().value("MaxPayloadSizeToTargetInBytes").toString();
                    if (!newSize.isEmpty()) {
                        uint32_t offered = newSize.toUInt();
                        if (offered > 0 && offered < maxPayloadSize) {
                            LOG_INFO_CAT(TAG, QString("Device counter-offered payload size: %1").arg(offered));
                            m_maxPayloadSize = offered;
                            // Retry with the offered size
                            xml = buildConfigureXml(storage, offered, skipStorageInit);
                            if (!sendXmlCommand(xml))
                                return false;
                            resp = receiveXmlResponse(XML_TIMEOUT_MS);
                            return resp.success;
                        }
                    }
                }
            }
        }
        return false;
    }

    LOG_INFO_CAT(TAG, "Firehose configured successfully");
    emit statusMessage("Firehose configured");
    return true;
}

// ─── Read GPT partitions ─────────────────────────────────────────────

QList<PartitionInfo> FirehoseClient::readGptPartitions(uint32_t lun)
{
    LOG_INFO_CAT(TAG, QString("Reading GPT from LUN %1").arg(lun));

    // Read first 64 sectors (enough for protective MBR + GPT header + entries)
    uint32_t gptSectors = (m_sectorSize == 4096) ? 8 : 64;
    QString xml = buildReadXml(0, gptSectors, m_sectorSize, lun);

    if (!sendXmlCommand(xml)) {
        LOG_ERROR_CAT(TAG, "Failed to send read GPT command");
        return {};
    }

    // Receive the raw GPT data
    uint32_t expectedSize = gptSectors * m_sectorSize;
    QByteArray gptData = m_transport->readExact(expectedSize, DATA_TIMEOUT_MS);
    if (gptData.size() != static_cast<int>(expectedSize)) {
        LOG_WARNING_CAT(TAG, QString("GPT read short: got %1/%2 bytes")
                                 .arg(gptData.size()).arg(expectedSize));
        if (gptData.isEmpty()) return {};
    }

    // Then receive the XML ACK
    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    if (!resp.success) {
        LOG_WARNING_CAT(TAG, "GPT read NAK'd, but data may still be valid");
    }

    // Parse GPT
    auto result = GptParser::parse(gptData, lun);
    if (!result.success) {
        LOG_ERROR_CAT(TAG, QString("GPT parse failed: %1").arg(result.errorMessage));
        return {};
    }

    LOG_INFO_CAT(TAG, QString("Found %1 partitions on LUN %2")
                    .arg(result.partitions.size()).arg(lun));
    return result.partitions;
}

// ─── Read partition ──────────────────────────────────────────────────

QByteArray FirehoseClient::readPartition(const QString& name, uint32_t lun,
                                          ProgressCallback progress)
{
    LOG_INFO_CAT(TAG, QString("Reading partition '%1' from LUN %2").arg(name).arg(lun));

    // First read GPT to find the partition
    auto partitions = readGptPartitions(lun);
    const PartitionInfo* target = nullptr;
    for (const auto& p : partitions) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            target = &p;
            break;
        }
    }

    if (!target) {
        LOG_ERROR_CAT(TAG, QString("Partition '%1' not found").arg(name));
        return {};
    }

    qint64 totalSectors = target->numSectors;
    qint64 totalBytes = totalSectors * m_sectorSize;
    qint64 readSoFar = 0;
    QByteArray result;
    result.reserve(totalBytes);

    uint32_t chunkSectors = m_maxPayloadSize / m_sectorSize;

    for (qint64 sector = 0; sector < totalSectors; sector += chunkSectors) {
        uint32_t count = qMin(static_cast<qint64>(chunkSectors), totalSectors - sector);
        uint64_t startSector = target->startSector + sector;

        QString xml = buildReadXml(startSector, count, m_sectorSize, lun);
        if (!sendXmlCommand(xml)) {
            LOG_ERROR_CAT(TAG, "Failed to send read command");
            return {};
        }

        uint32_t expectedBytes = count * m_sectorSize;
        QByteArray chunk = m_transport->readExact(expectedBytes, DATA_TIMEOUT_MS);
        if (chunk.size() != static_cast<int>(expectedBytes)) {
            LOG_WARNING_CAT(TAG, QString("readPartition: expected %1 bytes, got %2")
                                     .arg(expectedBytes).arg(chunk.size()));
            if (chunk.isEmpty()) {
                LOG_ERROR_CAT(TAG, "No data received, aborting read");
                return {};
            }
        }
        result.append(chunk);
        readSoFar += chunk.size();

        // Wait for ACK
        FirehoseResponse ackResp = receiveXmlResponse(XML_TIMEOUT_MS);
        if (!ackResp.success) {
            LOG_WARNING_CAT(TAG, QString("Read chunk NAK at sector %1: %2")
                                     .arg(startSector).arg(ackResp.rawValue));
        }

        if (progress)
            progress(readSoFar, totalBytes);
        emit transferProgress(readSoFar, totalBytes);
    }

    LOG_INFO_CAT(TAG, QString("Read %1 bytes from '%2'").arg(result.size()).arg(name));
    return result;
}

// ─── Write partition ─────────────────────────────────────────────────

bool FirehoseClient::writePartition(const QString& name, const QByteArray& data,
                                     uint32_t lun, ProgressCallback progress)
{
    LOG_INFO_CAT(TAG, QString("Writing %1 bytes to partition '%2' on LUN %3")
                    .arg(data.size()).arg(name).arg(lun));

    // Find partition in GPT
    auto partitions = readGptPartitions(lun);
    const PartitionInfo* target = nullptr;
    for (const auto& p : partitions) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            target = &p;
            break;
        }
    }

    if (!target) {
        LOG_ERROR_CAT(TAG, QString("Partition '%1' not found").arg(name));
        return false;
    }

    // Calculate sectors needed
    uint64_t numSectors = (data.size() + m_sectorSize - 1) / m_sectorSize;
    if (numSectors > target->numSectors) {
        LOG_ERROR_CAT(TAG, QString("Data too large: %1 sectors needed, %2 available")
                        .arg(numSectors).arg(target->numSectors));
        return false;
    }

    qint64 totalBytes = data.size();
    qint64 written = 0;
    uint32_t chunkSectors = m_maxPayloadSize / m_sectorSize;

    for (uint64_t sector = 0; sector < numSectors; sector += chunkSectors) {
        uint32_t count = qMin(static_cast<uint64_t>(chunkSectors), numSectors - sector);
        uint64_t startSector = target->startSector + sector;

        // Send program command
        QString xml = buildProgramXml(startSector, count, m_sectorSize, lun);
        if (!sendXmlCommand(xml)) {
            LOG_ERROR_CAT(TAG, "Failed to send program command");
            return false;
        }

        // Send the data chunk
        qint64 offset = static_cast<qint64>(sector) * m_sectorSize;
        uint32_t chunkSize = count * m_sectorSize;
        if (offset > data.size()) {
            LOG_ERROR_CAT(TAG, QString("Write offset %1 exceeds data size %2")
                                   .arg(offset).arg(data.size()));
            return false;
        }
        QByteArray chunk = data.mid(offset, chunkSize);

        // Pad to sector alignment
        if (static_cast<uint32_t>(chunk.size()) < chunkSize) {
            chunk.resize(chunkSize, '\0');
        }

        if (m_transport->write(chunk) != chunk.size()) {
            LOG_ERROR_CAT(TAG, "Failed to write data chunk");
            return false;
        }

        written += qMin(static_cast<qint64>(chunkSize), totalBytes - offset);

        // Wait for ACK
        FirehoseResponse resp = receiveXmlResponse(DATA_TIMEOUT_MS);
        if (!resp.success) {
            LOG_ERROR_CAT(TAG, QString("Write NAK at sector %1: %2")
                            .arg(startSector).arg(resp.rawValue));
            return false;
        }

        if (progress)
            progress(written, totalBytes);
        emit transferProgress(written, totalBytes);
    }

    LOG_INFO_CAT(TAG, QString("Write to '%1' complete").arg(name));
    return true;
}

// ─── Erase partition ─────────────────────────────────────────────────

bool FirehoseClient::erasePartition(const QString& name, uint32_t lun)
{
    LOG_INFO_CAT(TAG, QString("Erasing partition '%1' on LUN %2").arg(name).arg(lun));

    auto partitions = readGptPartitions(lun);
    const PartitionInfo* target = nullptr;
    for (const auto& p : partitions) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
            target = &p;
            break;
        }
    }

    if (!target) {
        LOG_ERROR_CAT(TAG, QString("Partition '%1' not found for erase").arg(name));
        return false;
    }

    QString xml = buildEraseXml(target->startSector, target->numSectors, m_sectorSize, lun);
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(DATA_TIMEOUT_MS);
    if (!resp.success) {
        LOG_ERROR_CAT(TAG, QString("Erase failed: %1").arg(resp.rawValue));
        return false;
    }

    LOG_INFO_CAT(TAG, QString("Partition '%1' erased").arg(name));
    return true;
}

// ─── Device control ──────────────────────────────────────────────────

bool FirehoseClient::reset()
{
    LOG_INFO_CAT(TAG, "Sending reset command");
    QString xml = buildPowerXml("reset");
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    return resp.success;
}

bool FirehoseClient::powerOff()
{
    LOG_INFO_CAT(TAG, "Sending power off command");
    QString xml = buildPowerXml("off");
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    return resp.success;
}

bool FirehoseClient::setActiveSlot(const QString& slot)
{
    LOG_INFO_CAT(TAG, QString("Setting active slot to '%1'").arg(slot));

    // Read all LUN partitions and patch the boot attributes
    // The active slot is controlled via partition attribute bits in GPT
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("setactiveslot");
    w.writeAttribute("slot", slot);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();

    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    return resp.success;
}

bool FirehoseClient::setBootableStorageDrive(uint32_t lun)
{
    LOG_INFO_CAT(TAG, QString("Setting bootable storage drive LUN %1").arg(lun));
    QString xml = buildSetBootableXml(lun);
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    return resp.success;
}

// ─── Raw XML ─────────────────────────────────────────────────────────

FirehoseResponse FirehoseClient::sendRawXml(const QString& xml)
{
    if (!sendXmlCommand(xml))
        return {};

    return receiveXmlResponse(XML_TIMEOUT_MS);
}

bool FirehoseClient::ping()
{
    LOG_DEBUG_CAT(TAG, "Ping");
    QString xml = QStringLiteral("<?xml version=\"1.0\" ?><data><nop /></data>");
    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(3000);
    return resp.success;
}

// ─── Peek / Poke ─────────────────────────────────────────────────────

QByteArray FirehoseClient::peekMemory(uint64_t address, uint32_t size)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("peek");
    w.writeAttribute("address64", QString("0x%1").arg(address, 16, 16, QChar('0')));
    w.writeAttribute("SizeInBytes", QString::number(size));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();

    if (!sendXmlCommand(xml))
        return {};

    QByteArray data = m_transport->readExact(size, DATA_TIMEOUT_MS);
    if (data.size() != static_cast<int>(size)) {
        LOG_WARNING_CAT(TAG, QString("peekMemory: expected %1 bytes, got %2")
                                 .arg(size).arg(data.size()));
    }
    receiveXmlResponse(XML_TIMEOUT_MS);
    return data;
}

bool FirehoseClient::pokeMemory(uint64_t address, const QByteArray& data)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");
    w.writeStartElement("poke");
    w.writeAttribute("address64", QString("0x%1").arg(address, 16, 16, QChar('0')));
    w.writeAttribute("SizeInBytes", QString::number(data.size()));
    w.writeAttribute("value", QString(data.toHex()));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();

    if (!sendXmlCommand(xml))
        return false;

    FirehoseResponse resp = receiveXmlResponse(XML_TIMEOUT_MS);
    return resp.success;
}

// ─── Write data helper ──────────────────────────────────────────────

bool FirehoseClient::writeDataChunked(const QByteArray& data, ProgressCallback progress)
{
    qint64 total = data.size();
    qint64 sent = 0;

    while (sent < total) {
        qint64 chunkSize = qMin(static_cast<qint64>(m_maxPayloadSize), total - sent);
        QByteArray chunk = data.mid(sent, chunkSize);

        // Pad to sector size if needed
        if (m_sectorSize > 0 && chunk.size() % m_sectorSize != 0) {
            int padded = ((chunk.size() / m_sectorSize) + 1) * m_sectorSize;
            chunk.resize(padded, '\0');
        }

        if (m_transport->write(chunk) != chunk.size()) {
            return false;
        }

        sent += chunkSize;
        if (progress)
            progress(sent, total);
    }

    return true;
}

} // namespace sakura
