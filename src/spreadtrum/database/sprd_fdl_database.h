#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

namespace sakura {

// ── Spreadtrum chip/FDL information ─────────────────────────────────────────

struct SprdChipInfo {
    uint16_t chipId = 0;
    QString  chipName;
    QString  marketingName;
    QString  architecture;
    uint32_t fdl1LoadAddr = 0;     // FDL1 load address in SRAM
    uint32_t fdl2LoadAddr = 0;     // FDL2 load address in DRAM
    uint32_t sramSize = 0;
    bool     supportsExploit = false;

    bool isValid() const { return chipId != 0; }
};

struct SprdFdlInfo {
    uint16_t chipId = 0;
    QString  fdl1Path;             // Path to FDL1 binary (relative)
    QString  fdl2Path;             // Path to FDL2 binary (relative)
    uint32_t fdl1LoadAddr = 0;
    uint32_t fdl2LoadAddr = 0;
    uint32_t fdl1EntryAddr = 0;
    uint32_t fdl2EntryAddr = 0;
    uint32_t baudRate = 921600;    // Default baud rate for FDL transfer

    bool isValid() const { return chipId != 0 && fdl1LoadAddr != 0; }
};

// ── FDL database singleton ──────────────────────────────────────────────────

class SprdFdlDatabase {
public:
    static SprdFdlDatabase& instance();

    // Chip lookup
    SprdChipInfo chipInfo(uint16_t chipId) const;
    QString chipName(uint16_t chipId) const;
    bool isKnownChip(uint16_t chipId) const;
    QList<SprdChipInfo> allChips() const;

    // FDL info
    SprdFdlInfo fdlForChip(uint16_t chipId) const;
    QList<uint16_t> allChipIds() const;

    // Filter
    QList<SprdChipInfo> chipsWithExploit() const;

private:
    SprdFdlDatabase();
    ~SprdFdlDatabase() = default;
    SprdFdlDatabase(const SprdFdlDatabase&) = delete;
    SprdFdlDatabase& operator=(const SprdFdlDatabase&) = delete;

    void initDatabase();

    QMap<uint16_t, SprdChipInfo> m_chips;
    QMap<uint16_t, SprdFdlInfo>  m_fdls;
};

} // namespace sakura
