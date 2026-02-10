#include "pac_parser.h"
#include "core/logger.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <cstring>

namespace sakura {

static constexpr char LOG_TAG[] = "SPRD-PAC";

// ── Parsing ─────────────────────────────────────────────────────────────────

bool PacParser::parse(const QString& filePath)
{
    m_valid = false;
    m_error.clear();
    m_filePath = filePath;
    m_info = {};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QString("Cannot open file: %1").arg(file.errorString());
        return false;
    }

    // Read main header
    QByteArray headerData = file.read(sizeof(PacFileHeader));
    if (headerData.size() < static_cast<int>(sizeof(PacFileHeader))) {
        m_error = "File too small for PAC header";
        return false;
    }

    if (!parseHeader(headerData))
        return false;

    LOG_INFO_CAT(LOG_TAG, QString("PAC: product='%1', firmware='%2', partitions=%3")
                              .arg(m_info.productName)
                              .arg(m_info.firmwareName)
                              .arg(m_info.header.partitionCount));

    // Read partition table
    uint32_t tableOffset = qFromLittleEndian(m_info.header.partitionTableOffset);
    uint32_t tableSize   = qFromLittleEndian(m_info.header.partitionTableSize);

    if (tableOffset + tableSize > static_cast<uint64_t>(file.size())) {
        m_error = "Partition table extends beyond file";
        return false;
    }

    file.seek(tableOffset);
    QByteArray tableData = file.read(tableSize);

    if (!parsePartitionTable(tableData))
        return false;

    file.close();
    m_valid = true;

    LOG_INFO_CAT(LOG_TAG, QString("Parsed %1 file entries").arg(m_info.files.size()));
    return true;
}

bool PacParser::parseHeader(const QByteArray& headerData)
{
    std::memcpy(&m_info.header, headerData.constData(), sizeof(PacFileHeader));

    uint32_t version = qFromLittleEndian(m_info.header.version);
    if (version == 0 || version > 0xFFFF) {
        m_error = QString("Invalid PAC version: 0x%1").arg(version, 8, 16, QChar('0'));
        return false;
    }

    // Read product and firmware names from file (UTF-16LE)
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    uint32_t prodOffset = qFromLittleEndian(m_info.header.productNameOffset);
    uint32_t prodLen    = qFromLittleEndian(m_info.header.productNameLength);
    if (prodOffset > 0 && prodLen > 0 && prodLen < 1024) {
        file.seek(prodOffset);
        QByteArray prodData = file.read(prodLen);
        m_info.productName = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(prodData.constData()),
            static_cast<int>(prodLen / 2));
    }

    uint32_t fwOffset = qFromLittleEndian(m_info.header.firmwareNameOffset);
    uint32_t fwLen    = qFromLittleEndian(m_info.header.firmwareNameLength);
    if (fwOffset > 0 && fwLen > 0 && fwLen < 1024) {
        file.seek(fwOffset);
        QByteArray fwData = file.read(fwLen);
        m_info.firmwareName = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(fwData.constData()),
            static_cast<int>(fwLen / 2));
    }

    file.close();
    return true;
}

bool PacParser::parsePartitionTable(const QByteArray& tableData)
{
    uint32_t count = qFromLittleEndian(m_info.header.partitionCount);
    int offset = 0;

    for (uint32_t i = 0; i < count; ++i) {
        if (offset + static_cast<int>(sizeof(PacPartitionHeader)) > tableData.size()) {
            LOG_WARNING_CAT(LOG_TAG, QString("Truncated partition table at entry %1").arg(i));
            break;
        }

        const auto* partHdr = reinterpret_cast<const PacPartitionHeader*>(
            tableData.constData() + offset);

        PacFileEntry entry;
        entry.partitionName = readUtf16String(partHdr->partitionName, 256);
        entry.fileName      = readUtf16String(partHdr->fileName, 256);

        uint64_t sizeHi  = qFromLittleEndian(partHdr->dataSizeHi);
        uint64_t sizeLo  = qFromLittleEndian(partHdr->dataSize);
        entry.size        = (sizeHi << 32) | sizeLo;

        uint64_t offHi   = qFromLittleEndian(partHdr->fileOffsetHi);
        uint64_t offLo   = qFromLittleEndian(partHdr->fileOffset);
        entry.dataOffset  = (offHi << 32) | offLo;

        uint64_t addrHi  = qFromLittleEndian(partHdr->addressHi);
        uint64_t addrLo  = qFromLittleEndian(partHdr->address);
        entry.address     = static_cast<uint32_t>(addrLo);
        Q_UNUSED(addrHi)

        entry.flags       = qFromLittleEndian(partHdr->flags);
        entry.usePartName = !entry.partitionName.isEmpty();

        if (entry.isValid()) {
            m_info.files.append(entry);
        }

        uint32_t partSize = qFromLittleEndian(partHdr->partitionSize);
        offset += (partSize > 0) ? static_cast<int>(partSize)
                                 : static_cast<int>(sizeof(PacPartitionHeader));
    }

    return true;
}

// ── Access ──────────────────────────────────────────────────────────────────

QList<PacFileEntry> PacParser::getPartitions() const
{
    QList<PacFileEntry> result;
    for (const auto& file : m_info.files) {
        if (file.usePartName)
            result.append(file);
    }
    return result;
}

QByteArray PacParser::readFileData(const PacFileEntry& entry) const
{
    if (m_filePath.isEmpty() || !entry.isValid())
        return {};

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    if (!file.seek(static_cast<qint64>(entry.dataOffset))) {
        LOG_ERROR_CAT(LOG_TAG, QString("Failed to seek to offset %1 for %2")
                                   .arg(entry.dataOffset).arg(entry.fileName));
        return {};
    }

    QByteArray data = file.read(static_cast<qint64>(entry.size));
    file.close();

    if (static_cast<uint64_t>(data.size()) != entry.size) {
        LOG_WARNING_CAT(LOG_TAG, QString("Read %1 bytes, expected %2 for %3")
                                     .arg(data.size()).arg(entry.size).arg(entry.fileName));
    }

    return data;
}

// ── Utilities ───────────────────────────────────────────────────────────────

bool PacParser::isPacFile(const QString& filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || fi.size() < static_cast<qint64>(sizeof(PacFileHeader)))
        return false;

    return fi.suffix().toLower() == "pac";
}

QString PacParser::readUtf16String(const wchar_t* data, int maxLen) const
{
    // Convert wchar_t string (UTF-16LE on Windows) to QString
    int len = 0;
    while (len < maxLen && data[len] != 0)
        ++len;

    return QString::fromWCharArray(data, len);
}

} // namespace sakura
