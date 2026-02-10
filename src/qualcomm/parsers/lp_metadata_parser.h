#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

namespace sakura {

// ─── Android LP (Logical Partitions) metadata structures ─────────────
// Used by Android's dynamic partition system (super partition).

// LP metadata geometry (located at offset 4096 in super partition)
struct LpMetadataGeometry {
    uint32_t magic = 0;             // LP_METADATA_GEOMETRY_MAGIC
    uint32_t structSize = 0;
    uint32_t checksum = 0;          // SHA-256 of the rest
    uint32_t metadataMaxSize = 0;
    uint32_t metadataSlotCount = 0;
    uint32_t logicalBlockSize = 0;
};

// LP partition entry
struct LpPartitionEntry {
    QString  name;
    QString  groupName;
    uint64_t firstExtentIndex = 0;
    uint32_t numExtents = 0;
    uint32_t attributes = 0;

    // Computed from extents
    uint64_t startOffset = 0;       // Byte offset in super
    uint64_t sizeBytes = 0;         // Total size
    bool     isSlotted = false;
};

// LP partition group
struct LpPartitionGroup {
    QString  name;
    uint64_t maxSize = 0;
    uint32_t flags = 0;
};

// LP extent (physical region)
struct LpExtent {
    uint64_t numSectors = 0;
    uint64_t physicalSector = 0;
    uint32_t targetType = 0;        // 0 = linear, 1 = zero
    uint32_t targetSource = 0;      // Block device index
};

// LP metadata parse result
struct LpMetadataResult {
    LpMetadataGeometry geometry;
    QList<LpPartitionEntry> partitions;
    QList<LpPartitionGroup> groups;
    QList<LpExtent> extents;
    bool     success = false;
    QString  errorMessage;
    uint32_t slotNumber = 0;        // 0 = _a, 1 = _b
};

// ─── LP Metadata parser ─────────────────────────────────────────────
class LpMetadataParser {
public:
    // Parse LP metadata from super partition data (or image file)
    static LpMetadataResult parse(const QByteArray& superData, uint32_t slot = 0);

    // Parse only the geometry header
    static LpMetadataGeometry parseGeometry(const QByteArray& data);

    // Check if data contains valid LP metadata
    static bool isValidLpMetadata(const QByteArray& data);

    // Get the total size of the super partition from geometry
    static uint64_t superPartitionSize(const LpMetadataGeometry& geo);

    // List partition names
    static QStringList partitionNames(const LpMetadataResult& result);

    // Constants
    static constexpr uint32_t LP_METADATA_GEOMETRY_MAGIC = 0x616c4467; // "gDla"
    static constexpr uint32_t LP_METADATA_HEADER_MAGIC   = 0x414c5030; // "0PLA"
    static constexpr uint32_t LP_GEOMETRY_OFFSET          = 4096;       // Primary geometry
    static constexpr uint32_t LP_GEOMETRY_BACKUP_OFFSET   = 8192;       // Backup geometry

private:
    struct LpMetadataHeader {
        uint32_t magic = 0;
        uint16_t majorVersion = 0;
        uint16_t minorVersion = 0;
        uint32_t headerSize = 0;
        uint32_t headerChecksum = 0;
        uint32_t tablesSize = 0;
        uint32_t tablesChecksum = 0;

        // Table descriptors
        uint32_t partitionsOffset = 0;
        uint32_t partitionsEntrySize = 0;
        uint32_t partitionsCount = 0;

        uint32_t extentsOffset = 0;
        uint32_t extentsEntrySize = 0;
        uint32_t extentsCount = 0;

        uint32_t groupsOffset = 0;
        uint32_t groupsEntrySize = 0;
        uint32_t groupsCount = 0;

        uint32_t blockDevicesOffset = 0;
        uint32_t blockDevicesEntrySize = 0;
        uint32_t blockDevicesCount = 0;
    };

    static LpMetadataHeader parseHeader(const QByteArray& data, uint32_t offset);
    static LpPartitionEntry parsePartition(const QByteArray& data, uint32_t offset);
    static LpExtent parseExtent(const QByteArray& data, uint32_t offset);
    static LpPartitionGroup parseGroup(const QByteArray& data, uint32_t offset);
};

} // namespace sakura
