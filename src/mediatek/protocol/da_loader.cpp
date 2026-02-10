#include "da_loader.h"
#include "core/logger.h"

#include <QFile>
#include <QtEndian>
#include <cstring>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-DA";

// ── Parsing ─────────────────────────────────────────────────────────────────

bool DaLoader::parseDaFile(const QByteArray& fileData)
{
    m_entries.clear();
    m_error.clear();

    if (fileData.size() < static_cast<int>(sizeof(DaFileHeader))) {
        m_error = "File too small to contain DA header";
        return false;
    }

    const auto* header = reinterpret_cast<const DaFileHeader*>(fileData.constData());

    // Validate magic ("MTK\0" or "MTK_")
    if (std::memcmp(header->magic, "MTK", 3) != 0) {
        // Some DA files don't have "MTK" magic — try parsing as raw entry table
        LOG_WARNING_CAT(LOG_TAG, QString("No MTK magic found (got 0x%1%2%3%4), attempting raw DA parse")
                                     .arg(static_cast<uint8_t>(header->magic[0]), 2, 16, QChar('0'))
                                     .arg(static_cast<uint8_t>(header->magic[1]), 2, 16, QChar('0'))
                                     .arg(static_cast<uint8_t>(header->magic[2]), 2, 16, QChar('0'))
                                     .arg(static_cast<uint8_t>(header->magic[3]), 2, 16, QChar('0')));
    }

    m_version = qFromLittleEndian(header->version);
    uint32_t entryCount = qFromLittleEndian(header->entryCount);

    LOG_INFO_CAT(LOG_TAG, QString("DA file version %1, %2 entries")
                              .arg(m_version).arg(entryCount));

    if (entryCount == 0 || entryCount > 256) {
        m_error = QString("Invalid DA entry count: %1").arg(entryCount);
        return false;
    }

    return parseEntryHeaders(fileData);
}

bool DaLoader::parseDaFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QString("Cannot open DA file: %1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    return parseDaFile(data);
}

bool DaLoader::parseEntryHeaders(const QByteArray& fileData)
{
    const auto* header = reinterpret_cast<const DaFileHeader*>(fileData.constData());
    uint32_t entryCount = qFromLittleEndian(header->entryCount);

    int offset = sizeof(DaFileHeader);

    for (uint32_t i = 0; i < entryCount; ++i) {
        if (offset + static_cast<int>(sizeof(DaEntryHeader)) > fileData.size()) {
            m_error = QString("Truncated DA entry header at index %1").arg(i);
            return false;
        }

        const auto* entryHdr = reinterpret_cast<const DaEntryHeader*>(
            fileData.constData() + offset);

        DaEntry entry;
        if (!extractEntryData(fileData, *entryHdr, entry)) {
            LOG_WARNING_CAT(LOG_TAG, QString("Skipping invalid DA entry %1").arg(i));
            offset += sizeof(DaEntryHeader);
            continue;
        }

        m_entries.append(entry);
        offset += sizeof(DaEntryHeader);
    }

    LOG_INFO_CAT(LOG_TAG, QString("Loaded %1 DA entries").arg(m_entries.size()));
    return !m_entries.isEmpty();
}

bool DaLoader::extractEntryData(const QByteArray& fileData, const DaEntryHeader& hdr,
                                 DaEntry& entry)
{
    entry.name       = QString::fromLatin1(hdr.name, strnlen(hdr.name, sizeof(hdr.name)));
    entry.hwCode     = static_cast<uint16_t>(qFromLittleEndian(hdr.hwCode) & 0xFFFF);
    entry.hwSubCode  = static_cast<uint16_t>(qFromLittleEndian(hdr.hwSubCode) & 0xFFFF);
    entry.loadAddr   = qFromLittleEndian(hdr.loadAddr);
    entry.entryAddr  = qFromLittleEndian(hdr.entryAddr);
    entry.signatureLen = qFromLittleEndian(hdr.signatureLen);
    entry.daType     = (qFromLittleEndian(hdr.type) == 0) ? DaType::DA1 : DaType::DA2;

    uint32_t dataOffset = qFromLittleEndian(hdr.dataOffset);
    uint32_t dataSize   = qFromLittleEndian(hdr.dataSize);

    if (dataOffset > static_cast<uint32_t>(fileData.size()) ||
        dataSize > static_cast<uint32_t>(fileData.size()) - dataOffset) {
        LOG_ERROR_CAT(LOG_TAG, QString("DA entry '%1' data out of bounds").arg(entry.name));
        return false;
    }

    entry.data = fileData.mid(static_cast<int>(dataOffset), static_cast<int>(dataSize));

    // Extract signature if present
    if (entry.signatureLen > 0 && entry.signatureLen <= entry.data.size()) {
        entry.signature = entry.data.right(static_cast<int>(entry.signatureLen));
    }

    return true;
}

// ── Retrieval ───────────────────────────────────────────────────────────────

DaEntry DaLoader::getDa1() const
{
    for (const auto& entry : m_entries) {
        if (entry.daType == DaType::DA1)
            return entry;
    }
    return {};
}

DaEntry DaLoader::getDa2() const
{
    for (const auto& entry : m_entries) {
        if (entry.daType == DaType::DA2)
            return entry;
    }
    return {};
}

DaEntry DaLoader::findDa1ForHwCode(uint16_t hwCode) const
{
    // First pass: exact HW code match for DA1 entries
    for (const auto& entry : m_entries) {
        if (entry.daType != DaType::DA1) continue;
        if (entry.hwCode == hwCode)
            return entry;
    }
    // Second pass: check for wildcard/zero hwCode (AllInOne DA files)
    for (const auto& entry : m_entries) {
        if (entry.daType != DaType::DA1) continue;
        if (entry.hwCode == 0)
            return entry;
    }
    // Fallback: return first DA1 (MTK AllInOne DA files bundle all
    // chips in a single entry; BROM selects the right code path).
    return getDa1();
}

DaEntry DaLoader::findDa2ForHwCode(uint16_t hwCode) const
{
    // First pass: exact HW code match for DA2 entries
    for (const auto& entry : m_entries) {
        if (entry.daType != DaType::DA2) continue;
        if (entry.hwCode == hwCode)
            return entry;
    }
    // Second pass: wildcard/zero hwCode
    for (const auto& entry : m_entries) {
        if (entry.daType != DaType::DA2) continue;
        if (entry.hwCode == 0)
            return entry;
    }
    // Fallback: return first DA2
    return getDa2();
}

} // namespace sakura
