#pragma once

#include <QString>
#include <QList>
#include <QUuid>
#include <cstdint>

namespace sakura {

struct PartitionInfo {
    QString name;
    uint64_t startSector = 0;
    uint64_t numSectors = 0;
    uint64_t sizeBytes = 0;
    uint32_t lun = 0;
    QUuid typeGuid;
    QUuid uniqueGuid;
    uint64_t attributes = 0;

    bool isSlotA() const { return name.endsWith("_a"); }
    bool isSlotB() const { return name.endsWith("_b"); }
    QString baseName() const {
        if (isSlotA()) return name.left(name.length() - 2);
        if (isSlotB()) return name.left(name.length() - 2);
        return name;
    }

    QString sizeHuman() const {
        if (sizeBytes >= (1ULL << 30))
            return QString("%1 GB").arg(sizeBytes / double(1ULL << 30), 0, 'f', 2);
        if (sizeBytes >= (1ULL << 20))
            return QString("%1 MB").arg(sizeBytes / double(1ULL << 20), 0, 'f', 2);
        if (sizeBytes >= (1ULL << 10))
            return QString("%1 KB").arg(sizeBytes / double(1ULL << 10), 0, 'f', 1);
        return QString("%1 B").arg(sizeBytes);
    }
};

struct GptHeader {
    uint64_t signature = 0;
    uint32_t revision = 0;
    uint32_t headerSize = 0;
    uint32_t headerCrc32 = 0;
    uint64_t myLba = 0;
    uint64_t alternateLba = 0;
    uint64_t firstUsableLba = 0;
    uint64_t lastUsableLba = 0;
    QUuid diskGuid;
    uint64_t partitionEntryLba = 0;
    uint32_t numberOfPartitions = 0;
    uint32_t partitionEntrySize = 0;
    uint32_t partitionEntryCrc32 = 0;
    uint32_t sectorSize = 512;
};

struct GptParseResult {
    GptHeader header;
    QList<PartitionInfo> partitions;
    uint32_t lun = 0;
    bool success = false;
    QString errorMessage;
};

enum class SlotState {
    NonExistent = 0,
    SlotA,
    SlotB,
    Unknown
};

struct SlotInfo {
    SlotState state = SlotState::NonExistent;
    QString currentSlot;
    QString otherSlot;
    bool hasAbPartitions = false;
};

} // namespace sakura
