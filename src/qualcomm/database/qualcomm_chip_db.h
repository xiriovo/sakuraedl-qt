#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

namespace sakura {

// ─── Chip database entry ─────────────────────────────────────────────
struct QualcommChipInfo {
    uint32_t msmId = 0;          // MSM HW ID
    QString  name;               // e.g. "SDM845", "SM8250"
    QString  codeName;           // e.g. "lahaina", "taro"
    QString  series;             // e.g. "Snapdragon 888"
    uint32_t jtagId = 0;        // JTAG TAP ID
    int      saharaVersion = 2; // Expected Sahara version
    bool     supportsUfs = true;
    uint32_t defaultSectorSize = 4096;
    QStringList knownDevices;    // List of known phone models
};

// ─── Qualcomm chip database ─────────────────────────────────────────
// Static lookup table mapping MSM IDs to chip information.
class QualcommChipDb {
public:
    // Lookup by MSM HW ID
    static QualcommChipInfo lookup(uint32_t msmId);

    // Lookup by chip name (case-insensitive)
    static QualcommChipInfo lookupByName(const QString& name);

    // Get all known chips
    static QList<QualcommChipInfo> allChips();

    // Check if a chip is known
    static bool isKnown(uint32_t msmId);

    // Get chip name for MSM ID (returns hex string if unknown)
    static QString chipNameForMsm(uint32_t msmId);

private:
    static void initDb();
    static QMap<uint32_t, QualcommChipInfo> s_database;
    static bool s_initialized;
};

} // namespace sakura
