#include "lp_metadata_parser.h"
#include "core/logger.h"

#include <cstring>

static const QString TAG = QStringLiteral("LpMetadata");

namespace sakura {

// ─── Geometry parsing ────────────────────────────────────────────────

LpMetadataGeometry LpMetadataParser::parseGeometry(const QByteArray& data)
{
    LpMetadataGeometry geo;

    if (data.size() < static_cast<int>(LP_GEOMETRY_OFFSET + sizeof(LpMetadataGeometry))) {
        return geo;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData()) + LP_GEOMETRY_OFFSET;

    std::memcpy(&geo.magic, p, 4);
    std::memcpy(&geo.structSize, p + 4, 4);
    std::memcpy(&geo.checksum, p + 8, 4);
    std::memcpy(&geo.metadataMaxSize, p + 12, 4);
    std::memcpy(&geo.metadataSlotCount, p + 16, 4);
    std::memcpy(&geo.logicalBlockSize, p + 20, 4);

    return geo;
}

bool LpMetadataParser::isValidLpMetadata(const QByteArray& data)
{
    if (data.size() < static_cast<int>(LP_GEOMETRY_OFFSET + 24))
        return false;

    uint32_t magic = 0;
    std::memcpy(&magic, data.constData() + LP_GEOMETRY_OFFSET, 4);
    return magic == LP_METADATA_GEOMETRY_MAGIC;
}

// ─── Header parsing ──────────────────────────────────────────────────

LpMetadataParser::LpMetadataHeader LpMetadataParser::parseHeader(const QByteArray& data, uint32_t offset)
{
    LpMetadataHeader hdr;

    if (static_cast<uint32_t>(data.size()) < offset + 128)
        return hdr;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData()) + offset;
    uint32_t pos = 0;

    auto read32 = [&]() -> uint32_t {
        uint32_t val = 0;
        std::memcpy(&val, p + pos, 4);
        pos += 4;
        return val;
    };
    auto read16 = [&]() -> uint16_t {
        uint16_t val = 0;
        std::memcpy(&val, p + pos, 2);
        pos += 2;
        return val;
    };

    hdr.magic            = read32();
    hdr.majorVersion     = read16();
    hdr.minorVersion     = read16();
    hdr.headerSize       = read32();
    hdr.headerChecksum   = read32();
    hdr.tablesSize       = read32();
    hdr.tablesChecksum   = read32();

    hdr.partitionsOffset     = read32();
    hdr.partitionsEntrySize  = read32();
    hdr.partitionsCount      = read32();

    hdr.extentsOffset        = read32();
    hdr.extentsEntrySize     = read32();
    hdr.extentsCount         = read32();

    hdr.groupsOffset         = read32();
    hdr.groupsEntrySize      = read32();
    hdr.groupsCount          = read32();

    hdr.blockDevicesOffset     = read32();
    hdr.blockDevicesEntrySize  = read32();
    hdr.blockDevicesCount      = read32();

    return hdr;
}

// ─── Partition entry parsing ─────────────────────────────────────────

LpPartitionEntry LpMetadataParser::parsePartition(const QByteArray& data, uint32_t offset)
{
    LpPartitionEntry entry;

    if (static_cast<uint32_t>(data.size()) < offset + 52)
        return entry;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData()) + offset;

    // Partition name: 36 bytes (null-terminated UTF-8)
    char name[37] = {};
    std::memcpy(name, p, 36);
    entry.name = QString::fromUtf8(name).trimmed();

    // Attributes at offset 36
    std::memcpy(&entry.attributes, p + 36, 4);

    // First extent index at offset 40
    std::memcpy(&entry.firstExtentIndex, p + 40, 4);

    // Number of extents at offset 44
    std::memcpy(&entry.numExtents, p + 44, 4);

    // Group index at offset 48
    uint32_t groupIdx = 0;
    std::memcpy(&groupIdx, p + 48, 4);

    entry.isSlotted = entry.name.endsWith("_a") || entry.name.endsWith("_b");

    return entry;
}

// ─── Extent parsing ──────────────────────────────────────────────────

LpExtent LpMetadataParser::parseExtent(const QByteArray& data, uint32_t offset)
{
    LpExtent ext;

    if (static_cast<uint32_t>(data.size()) < offset + 24)
        return ext;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData()) + offset;

    std::memcpy(&ext.numSectors, p, 8);
    std::memcpy(&ext.targetType, p + 8, 4);
    std::memcpy(&ext.physicalSector, p + 12, 8);
    std::memcpy(&ext.targetSource, p + 20, 4);

    return ext;
}

// ─── Group parsing ───────────────────────────────────────────────────

LpPartitionGroup LpMetadataParser::parseGroup(const QByteArray& data, uint32_t offset)
{
    LpPartitionGroup group;

    if (static_cast<uint32_t>(data.size()) < offset + 48)
        return group;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData()) + offset;

    char name[37] = {};
    std::memcpy(name, p, 36);
    group.name = QString::fromUtf8(name).trimmed();

    std::memcpy(&group.flags, p + 36, 4);
    std::memcpy(&group.maxSize, p + 40, 8);

    return group;
}

// ─── Full metadata parsing ───────────────────────────────────────────

LpMetadataResult LpMetadataParser::parse(const QByteArray& superData, uint32_t slot)
{
    LpMetadataResult result;
    result.slotNumber = slot;

    // Parse geometry
    result.geometry = parseGeometry(superData);
    if (result.geometry.magic != LP_METADATA_GEOMETRY_MAGIC) {
        result.errorMessage = "Invalid LP metadata geometry magic";
        LOG_ERROR_CAT(TAG, result.errorMessage);
        return result;
    }

    LOG_INFO_CAT(TAG, QString("LP geometry: blockSize=%1, maxMeta=%2, slots=%3")
                    .arg(result.geometry.logicalBlockSize)
                    .arg(result.geometry.metadataMaxSize)
                    .arg(result.geometry.metadataSlotCount));

    // Metadata header follows the two geometry copies
    // Primary metadata starts after geometry (2 copies of geometry, each up to 4096 bytes)
    uint32_t metadataOffset = LP_GEOMETRY_OFFSET + 2 * 4096;
    metadataOffset += slot * result.geometry.metadataMaxSize;

    LpMetadataHeader hdr = parseHeader(superData, metadataOffset);
    if (hdr.magic != LP_METADATA_HEADER_MAGIC) {
        result.errorMessage = QString("Invalid metadata header magic at offset %1").arg(metadataOffset);
        LOG_ERROR_CAT(TAG, result.errorMessage);
        return result;
    }

    LOG_INFO_CAT(TAG, QString("LP metadata v%1.%2: %3 partitions, %4 extents, %5 groups")
                    .arg(hdr.majorVersion).arg(hdr.minorVersion)
                    .arg(hdr.partitionsCount).arg(hdr.extentsCount)
                    .arg(hdr.groupsCount));

    uint32_t tablesBase = metadataOffset + hdr.headerSize;

    // Parse groups
    for (uint32_t i = 0; i < hdr.groupsCount; ++i) {
        uint32_t off = tablesBase + hdr.groupsOffset + i * hdr.groupsEntrySize;
        result.groups.append(parseGroup(superData, off));
    }

    // Parse extents
    for (uint32_t i = 0; i < hdr.extentsCount; ++i) {
        uint32_t off = tablesBase + hdr.extentsOffset + i * hdr.extentsEntrySize;
        result.extents.append(parseExtent(superData, off));
    }

    // Parse partitions
    for (uint32_t i = 0; i < hdr.partitionsCount; ++i) {
        uint32_t off = tablesBase + hdr.partitionsOffset + i * hdr.partitionsEntrySize;
        LpPartitionEntry entry = parsePartition(superData, off);

        // Resolve size from extents
        uint64_t totalSize = 0;
        for (uint32_t e = 0; e < entry.numExtents; ++e) {
            uint32_t extIdx = entry.firstExtentIndex + e;
            if (extIdx < static_cast<uint32_t>(result.extents.size())) {
                totalSize += result.extents[extIdx].numSectors * 512;
                if (entry.startOffset == 0 && e == 0) {
                    entry.startOffset = result.extents[extIdx].physicalSector * 512;
                }
            }
        }
        entry.sizeBytes = totalSize;

        result.partitions.append(entry);
    }

    result.success = true;
    LOG_INFO_CAT(TAG, QString("Parsed %1 LP partitions").arg(result.partitions.size()));
    return result;
}

uint64_t LpMetadataParser::superPartitionSize(const LpMetadataGeometry& geo)
{
    // Super size can be derived from the metadata layout
    // Typically: 2 * geometry + slots * metadataMaxSize + actual partition data
    return static_cast<uint64_t>(geo.metadataMaxSize) * geo.metadataSlotCount * 2 +
           LP_GEOMETRY_OFFSET + 2 * 4096;
}

QStringList LpMetadataParser::partitionNames(const LpMetadataResult& result)
{
    QStringList names;
    for (const auto& p : result.partitions) {
        if (!p.name.isEmpty())
            names.append(p.name);
    }
    return names;
}

} // namespace sakura
