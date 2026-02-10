#pragma once

#include "partition_info.h"
#include <QByteArray>

namespace sakura {

class GptParser {
public:
    static GptParseResult parse(const QByteArray& data, uint32_t lun = 0);
    static bool isValidGpt(const QByteArray& data);
    static uint32_t detectSectorSize(const QByteArray& data);

    // Generate rawprogram/patch XML from parsed GPT
    static QString generateRawprogramXml(const QList<PartitionInfo>& partitions, uint32_t lun);
    static QString generatePatchXml(const GptHeader& header, uint32_t lun);

    // Detect A/B slot from partition attributes
    static SlotInfo detectSlot(const QList<PartitionInfo>& partitions);

private:
    static GptHeader parseHeader(const uint8_t* data, uint32_t sectorSize);
    static PartitionInfo parseEntry(const uint8_t* data, uint32_t sectorSize);
    static QUuid readGuid(const uint8_t* data);

    static constexpr uint64_t GPT_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"
};

} // namespace sakura
