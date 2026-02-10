#include "sprd_fdl_database.h"

namespace sakura {

SprdFdlDatabase& SprdFdlDatabase::instance()
{
    static SprdFdlDatabase db;
    return db;
}

SprdFdlDatabase::SprdFdlDatabase()
{
    initDatabase();
}

// ── Chip lookup ─────────────────────────────────────────────────────────────

SprdChipInfo SprdFdlDatabase::chipInfo(uint16_t chipId) const
{
    return m_chips.value(chipId, {});
}

QString SprdFdlDatabase::chipName(uint16_t chipId) const
{
    auto it = m_chips.constFind(chipId);
    if (it != m_chips.constEnd())
        return it->chipName;
    return QString("Unknown (0x%1)").arg(chipId, 4, 16, QChar('0'));
}

bool SprdFdlDatabase::isKnownChip(uint16_t chipId) const
{
    return m_chips.contains(chipId);
}

QList<SprdChipInfo> SprdFdlDatabase::allChips() const
{
    return m_chips.values();
}

// ── FDL info ────────────────────────────────────────────────────────────────

SprdFdlInfo SprdFdlDatabase::fdlForChip(uint16_t chipId) const
{
    return m_fdls.value(chipId, {});
}

QList<uint16_t> SprdFdlDatabase::allChipIds() const
{
    return m_chips.keys();
}

QList<SprdChipInfo> SprdFdlDatabase::chipsWithExploit() const
{
    QList<SprdChipInfo> result;
    for (const auto& chip : m_chips) {
        if (chip.supportsExploit)
            result.append(chip);
    }
    return result;
}

// ── Database initialization ─────────────────────────────────────────────────

void SprdFdlDatabase::initDatabase()
{
    auto addChip = [this](uint16_t id, const QString& name, const QString& marketing,
                          const QString& arch, uint32_t fdl1Addr, uint32_t fdl2Addr,
                          uint32_t sram, bool exploit) {
        SprdChipInfo chip;
        chip.chipId          = id;
        chip.chipName        = name;
        chip.marketingName   = marketing;
        chip.architecture    = arch;
        chip.fdl1LoadAddr    = fdl1Addr;
        chip.fdl2LoadAddr    = fdl2Addr;
        chip.sramSize        = sram;
        chip.supportsExploit = exploit;
        m_chips[id] = chip;

        SprdFdlInfo fdl;
        fdl.chipId       = id;
        fdl.fdl1LoadAddr = fdl1Addr;
        fdl.fdl2LoadAddr = fdl2Addr;
        fdl.fdl1EntryAddr = fdl1Addr;
        fdl.fdl2EntryAddr = fdl2Addr;
        m_fdls[id] = fdl;
    };

    //            id      name        marketing       arch          fdl1Addr     fdl2Addr     sram     exploit
    addChip(0x7715, "SC7715",   "SC7715",       "Cortex-A7",    0x00003000, 0x80008000, 0x10000, false);
    addChip(0x7727, "SC7727",   "SC7727",       "Cortex-A7",    0x00003000, 0x80008000, 0x10000, false);
    addChip(0x7730, "SC7730",   "SC7730",       "Cortex-A7",    0x00003000, 0x80008000, 0x10000, false);
    addChip(0x7731, "SC7731",   "SC7731",       "Cortex-A7",    0x00003000, 0x80008000, 0x10000, true);
    addChip(0x7731, "SC7731E",  "SC7731E",      "Cortex-A7",    0x00003000, 0x80008000, 0x10000, true);
    addChip(0x9830, "SC9830",   "SC9830",       "Cortex-A7",    0x50003000, 0x80008000, 0x20000, false);
    addChip(0x9832, "SC9832",   "SC9832",       "Cortex-A53",   0x50003000, 0x80008000, 0x20000, true);
    addChip(0x9832, "SC9832E",  "SC9832E",      "Cortex-A53",   0x50003000, 0x80008000, 0x20000, true);
    addChip(0x9850, "SC9850",   "SC9850",       "Cortex-A53",   0x50003000, 0x80008000, 0x40000, true);
    addChip(0x9853, "SC9853I",  "SC9853I",      "Intel x86",    0x50003000, 0x80008000, 0x40000, false);
    addChip(0x9860, "SC9860",   "SC9860",       "Cortex-A53",   0x50003000, 0x80008000, 0x40000, false);
    addChip(0x9863, "SC9863A",  "SC9863A",      "Cortex-A55",   0x00005000, 0x80008000, 0x40000, true);

    // Unisoc Tiger series
    addChip(0x2721, "UMS512",   "T610",         "Cortex-A75+A55", 0x00005000, 0x80008000, 0x40000, false);
    addChip(0x2722, "UMS9230",  "T606",         "Cortex-A75+A55", 0x00005000, 0x80008000, 0x40000, false);
    addChip(0x2723, "UMS9620",  "T618",         "Cortex-A75+A55", 0x00005000, 0x80008000, 0x40000, false);
    addChip(0x2730, "UMS9120",  "T700",         "Cortex-A76+A55", 0x00005000, 0x80008000, 0x40000, false);
    addChip(0x2731, "UMS9230",  "T760",         "Cortex-A76+A55", 0x00005000, 0x80008000, 0x40000, false);
    addChip(0x2740, "UMS9520",  "T820",         "Cortex-A78+A55", 0x00005000, 0x80008000, 0x40000, false);
}

} // namespace sakura
