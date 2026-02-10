#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

namespace sakura {

// ── MTK chip information ────────────────────────────────────────────────────

struct MtkChipInfo {
    uint16_t hwCode = 0;
    uint16_t hwSubCode = 0;
    QString  chipName;
    QString  marketingName;          // e.g., "Helio P60", "Dimensity 700"
    QString  architecture;           // e.g., "Cortex-A73+A53"
    uint32_t bromVersion = 0;
    bool     supportsXFlash = false;
    bool     supportsXmlDa = false;
    bool     supportsExploit = false;

    // DA-related info
    uint32_t daLoadAddr = 0;
    uint32_t sramSize = 0;

    bool isValid() const { return hwCode != 0; }
};

// ── Chip database singleton ─────────────────────────────────────────────────

class MtkChipDatabase {
public:
    static MtkChipDatabase& instance();

    // Lookup
    MtkChipInfo chipInfo(uint16_t hwCode) const;
    QString chipName(uint16_t hwCode) const;
    QString marketingName(uint16_t hwCode) const;

    // Query
    bool isKnownChip(uint16_t hwCode) const;
    QList<MtkChipInfo> allChips() const;
    QList<uint16_t> allHwCodes() const;

    // Filter
    QList<MtkChipInfo> chipsWithExploit() const;
    QList<MtkChipInfo> chipsWithXFlash() const;

private:
    MtkChipDatabase();
    ~MtkChipDatabase() = default;
    MtkChipDatabase(const MtkChipDatabase&) = delete;
    MtkChipDatabase& operator=(const MtkChipDatabase&) = delete;

    void initDatabase();

    QMap<uint16_t, MtkChipInfo> m_chips;
};

} // namespace sakura
