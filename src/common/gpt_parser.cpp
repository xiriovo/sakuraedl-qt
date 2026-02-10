#include "gpt_parser.h"
#include "crc_utils.h"
#include "core/logger.h"
#include <cstring>

namespace sakura {

bool GptParser::isValidGpt(const QByteArray& data)
{
    if (data.size() < 1024) return false;
    // Check for EFI PART signature at sector 1 (offset 512) or sector 2 (offset 1024)
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());
    uint64_t sig512 = 0, sig1024 = 0, sig4096 = 0;
    if (data.size() >= 520)  std::memcpy(&sig512, d + 512, 8);
    if (data.size() >= 1032) std::memcpy(&sig1024, d + 1024, 8);
    if (data.size() >= 4104) std::memcpy(&sig4096, d + 4096, 8);
    return sig512 == GPT_SIGNATURE || sig1024 == GPT_SIGNATURE || sig4096 == GPT_SIGNATURE;
}

uint32_t GptParser::detectSectorSize(const QByteArray& data)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());
    // Try common sector sizes: 512, 4096
    for (uint32_t ss : {512u, 4096u}) {
        if (static_cast<uint32_t>(data.size()) < ss * 2 + 8) continue;
        uint64_t sig;
        std::memcpy(&sig, d + ss, 8);
        if (sig == GPT_SIGNATURE)
            return ss;
    }
    return 512; // default
}

GptParseResult GptParser::parse(const QByteArray& data, uint32_t lun)
{
    GptParseResult result;
    result.lun = lun;

    if (data.size() < 2048) {
        result.errorMessage = "Data too small for GPT";
        return result;
    }

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());
    uint32_t sectorSize = detectSectorSize(data);

    // Parse GPT header at LBA 1
    if (static_cast<uint32_t>(data.size()) < sectorSize + 92) {
        result.errorMessage = "Data too small for GPT header";
        return result;
    }

    result.header = parseHeader(d + sectorSize, sectorSize);

    if (result.header.signature != GPT_SIGNATURE) {
        result.errorMessage = "Invalid GPT signature";
        return result;
    }

    // Validate header CRC32
    uint8_t headerCopy[92];
    std::memcpy(headerCopy, d + sectorSize, 92);
    // Zero out the CRC field for verification
    std::memset(headerCopy + 16, 0, 4);
    uint32_t computedCrc = Crc32::compute(headerCopy, result.header.headerSize);
    if (computedCrc != result.header.headerCrc32) {
        LOG_WARNING(QString("GPT header CRC mismatch: expected=%1 computed=%2")
                        .arg(result.header.headerCrc32, 8, 16, QChar('0'))
                        .arg(computedCrc, 8, 16, QChar('0')));
    }

    // Parse partition entries
    uint64_t entriesOffset = result.header.partitionEntryLba * sectorSize;
    uint32_t entrySize = result.header.partitionEntrySize;
    uint32_t numParts = result.header.numberOfPartitions;

    for (uint32_t i = 0; i < numParts; i++) {
        uint64_t offset = entriesOffset + i * entrySize;
        if (offset + entrySize > static_cast<uint64_t>(data.size())) break;

        PartitionInfo part = parseEntry(d + offset, sectorSize);
        if (part.name.isEmpty()) continue; // Empty entry
        part.lun = lun;
        result.partitions.append(part);
    }

    result.header.sectorSize = sectorSize;
    result.success = true;
    LOG_INFO(QString("GPT parsed: %1 partitions, sector=%2, LUN=%3")
                 .arg(result.partitions.size()).arg(sectorSize).arg(lun));
    return result;
}

GptHeader GptParser::parseHeader(const uint8_t* data, uint32_t sectorSize)
{
    GptHeader h;
    std::memcpy(&h.signature, data, 8);
    std::memcpy(&h.revision, data + 8, 4);
    std::memcpy(&h.headerSize, data + 12, 4);
    std::memcpy(&h.headerCrc32, data + 16, 4);
    std::memcpy(&h.myLba, data + 24, 8);
    std::memcpy(&h.alternateLba, data + 32, 8);
    std::memcpy(&h.firstUsableLba, data + 40, 8);
    std::memcpy(&h.lastUsableLba, data + 48, 8);
    h.diskGuid = readGuid(data + 56);
    std::memcpy(&h.partitionEntryLba, data + 72, 8);
    std::memcpy(&h.numberOfPartitions, data + 80, 4);
    std::memcpy(&h.partitionEntrySize, data + 84, 4);
    std::memcpy(&h.partitionEntryCrc32, data + 88, 4);
    h.sectorSize = sectorSize;
    return h;
}

PartitionInfo GptParser::parseEntry(const uint8_t* data, uint32_t sectorSize)
{
    PartitionInfo p;
    p.typeGuid = readGuid(data);
    p.uniqueGuid = readGuid(data + 16);

    // Check if entry is empty (all-zero type GUID)
    if (p.typeGuid.isNull()) return {};

    std::memcpy(&p.startSector, data + 32, 8);
    uint64_t endSector;
    std::memcpy(&endSector, data + 40, 8);
    p.numSectors = (endSector >= p.startSector) ? (endSector - p.startSector + 1) : 0;
    p.sizeBytes = p.numSectors * sectorSize;
    std::memcpy(&p.attributes, data + 48, 8);

    // Read name (UTF-16LE, up to 72 bytes = 36 chars)
    QString name;
    for (int i = 0; i < 36; i++) {
        uint16_t ch;
        std::memcpy(&ch, data + 56 + i * 2, 2);
        if (ch == 0) break;
        name.append(QChar(ch));
    }
    p.name = name;
    return p;
}

QUuid GptParser::readGuid(const uint8_t* data)
{
    // GPT GUID uses mixed-endian encoding
    uint32_t d1;
    uint16_t d2, d3;
    std::memcpy(&d1, data, 4);
    std::memcpy(&d2, data + 4, 2);
    std::memcpy(&d3, data + 6, 2);
    return QUuid(d1, d2, d3, data[8], data[9], data[10], data[11],
                 data[12], data[13], data[14], data[15]);
}

SlotInfo GptParser::detectSlot(const QList<PartitionInfo>& partitions)
{
    SlotInfo info;
    int slotACount = 0, slotBCount = 0;

    for (const auto& p : partitions) {
        if (p.isSlotA()) slotACount++;
        if (p.isSlotB()) slotBCount++;
    }

    info.hasAbPartitions = (slotACount > 0 && slotBCount > 0);
    if (!info.hasAbPartitions) {
        info.state = SlotState::NonExistent;
        return info;
    }

    // Detect active slot from boot_a/boot_b attributes
    for (const auto& p : partitions) {
        if (p.name == "boot_a" || p.name == "boot_b") {
            bool active = (p.attributes >> 48) & 0x1; // Active flag bit
            if (active) {
                if (p.name == "boot_a") {
                    info.state = SlotState::SlotA;
                    info.currentSlot = "_a";
                    info.otherSlot = "_b";
                } else {
                    info.state = SlotState::SlotB;
                    info.currentSlot = "_b";
                    info.otherSlot = "_a";
                }
                break;
            }
        }
    }

    if (info.state == SlotState::NonExistent)
        info.state = SlotState::Unknown;

    return info;
}

QString GptParser::generateRawprogramXml(const QList<PartitionInfo>& partitions, uint32_t lun)
{
    QString xml = "<?xml version=\"1.0\"?>\n<data>\n";
    for (const auto& p : partitions) {
        xml += QString("  <program SECTOR_SIZE_IN_BYTES=\"512\" "
                        "file_sector_offset=\"0\" filename=\"\" "
                        "label=\"%1\" num_partition_sectors=\"%2\" "
                        "physical_partition_number=\"%3\" "
                        "start_sector=\"%4\" />\n")
                   .arg(p.name).arg(p.numSectors).arg(lun).arg(p.startSector);
    }
    xml += "</data>\n";
    return xml;
}

QString GptParser::generatePatchXml(const GptHeader& header, uint32_t lun)
{
    // Generate patch XML entries that update CRC32 fields in the primary
    // and backup GPT headers after partition table modifications.
    //
    // The Qualcomm Firehose protocol uses patch0.xml to fix up GPT CRCs:
    //   - Primary GPT header CRC32     at LBA1 offset 16  (4 bytes)
    //   - Primary GPT entry array CRC  at LBA1 offset 88  (4 bytes)
    //   - Backup GPT header CRC32      at last LBA offset 16 (4 bytes)
    //   - Backup GPT entry array CRC   at last LBA offset 88 (4 bytes)

    uint32_t ss = header.sectorSize > 0 ? header.sectorSize : 512;

    QString xml = "<?xml version=\"1.0\"?>\n<patches>\n";

    // Patch 1: Primary header CRC32 at LBA 1, byte offset 16
    xml += QString("  <patch SECTOR_SIZE_IN_BYTES=\"%1\" "
                   "byte_offset=\"16\" filename=\"DISK\" "
                   "physical_partition_number=\"%2\" "
                   "size_in_bytes=\"4\" "
                   "start_sector=\"1\" "
                   "value=\"NUM_DISK_SECTORS-1.\" what=\"Update Primary GPT Header CRC\" />\n")
               .arg(ss).arg(lun);

    // Patch 2: Primary header's partition-entry-array CRC at LBA 1, byte offset 88
    xml += QString("  <patch SECTOR_SIZE_IN_BYTES=\"%1\" "
                   "byte_offset=\"88\" filename=\"DISK\" "
                   "physical_partition_number=\"%2\" "
                   "size_in_bytes=\"4\" "
                   "start_sector=\"1\" "
                   "value=\"0\" what=\"Update Partition Entry Array CRC\" />\n")
               .arg(ss).arg(lun);

    // Patch 3: Backup GPT header CRC at last LBA, byte offset 16
    xml += QString("  <patch SECTOR_SIZE_IN_BYTES=\"%1\" "
                   "byte_offset=\"16\" filename=\"DISK\" "
                   "physical_partition_number=\"%2\" "
                   "size_in_bytes=\"4\" "
                   "start_sector=\"NUM_DISK_SECTORS-1.\" "
                   "value=\"NUM_DISK_SECTORS-1.\" what=\"Update Backup GPT Header CRC\" />\n")
               .arg(ss).arg(lun);

    // Patch 4: Backup GPT entry array CRC at last LBA, byte offset 88
    xml += QString("  <patch SECTOR_SIZE_IN_BYTES=\"%1\" "
                   "byte_offset=\"88\" filename=\"DISK\" "
                   "physical_partition_number=\"%2\" "
                   "size_in_bytes=\"4\" "
                   "start_sector=\"NUM_DISK_SECTORS-1.\" "
                   "value=\"0\" what=\"Update Backup Partition Entry Array CRC\" />\n")
               .arg(ss).arg(lun);

    xml += "</patches>\n";
    return xml;
}

} // namespace sakura
