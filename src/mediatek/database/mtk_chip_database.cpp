#include "mtk_chip_database.h"

namespace sakura {

MtkChipDatabase& MtkChipDatabase::instance()
{
    static MtkChipDatabase db;
    return db;
}

MtkChipDatabase::MtkChipDatabase()
{
    initDatabase();
}

// ── Lookup ──────────────────────────────────────────────────────────────────

MtkChipInfo MtkChipDatabase::chipInfo(uint16_t hwCode) const
{
    return m_chips.value(hwCode, {});
}

QString MtkChipDatabase::chipName(uint16_t hwCode) const
{
    auto it = m_chips.constFind(hwCode);
    if (it != m_chips.constEnd())
        return it->chipName;
    return QString("Unknown (0x%1)").arg(hwCode, 4, 16, QChar('0'));
}

QString MtkChipDatabase::marketingName(uint16_t hwCode) const
{
    auto it = m_chips.constFind(hwCode);
    if (it != m_chips.constEnd())
        return it->marketingName;
    return {};
}

// ── Query ───────────────────────────────────────────────────────────────────

bool MtkChipDatabase::isKnownChip(uint16_t hwCode) const
{
    return m_chips.contains(hwCode);
}

QList<MtkChipInfo> MtkChipDatabase::allChips() const
{
    return m_chips.values();
}

QList<uint16_t> MtkChipDatabase::allHwCodes() const
{
    return m_chips.keys();
}

QList<MtkChipInfo> MtkChipDatabase::chipsWithExploit() const
{
    QList<MtkChipInfo> result;
    for (const auto& chip : m_chips) {
        if (chip.supportsExploit)
            result.append(chip);
    }
    return result;
}

QList<MtkChipInfo> MtkChipDatabase::chipsWithXFlash() const
{
    QList<MtkChipInfo> result;
    for (const auto& chip : m_chips) {
        if (chip.supportsXFlash)
            result.append(chip);
    }
    return result;
}

// ── Database initialization ─────────────────────────────────────────────────

void MtkChipDatabase::initDatabase()
{
    auto add = [this](uint16_t hwCode, uint16_t hwSub,
                      const QString& name, const QString& marketing,
                      const QString& arch, bool xflash, bool xmlDa,
                      bool exploit, uint32_t daLoad = 0x200000,
                      uint32_t sram = 0x20000) {
        MtkChipInfo info;
        info.hwCode        = hwCode;
        info.hwSubCode     = hwSub;
        info.chipName      = name;
        info.marketingName = marketing;
        info.architecture  = arch;
        info.supportsXFlash  = xflash;
        info.supportsXmlDa   = xmlDa;
        info.supportsExploit = exploit;
        info.daLoadAddr    = daLoad;
        info.sramSize      = sram;
        m_chips[hwCode] = info;
    };

    //           hwCode  sub   name       marketing            arch                xf  xml expl
    add(0x0279, 0x8A00, "MT6797",  "Helio X20",       "Cortex-A72+A53",    true,  false, true);
    add(0x0321, 0x8A00, "MT6735",  "MT6735",          "Cortex-A53",        false, false, true);
    add(0x0326, 0x8A00, "MT6750",  "MT6750",          "Cortex-A53",        false, false, true);
    add(0x0335, 0x8A00, "MT6737",  "MT6737",          "Cortex-A53",        false, false, true);
    add(0x0337, 0x8A00, "MT6753",  "MT6753",          "Cortex-A53",        false, false, true);
    add(0x0551, 0x8A00, "MT6755",  "Helio P10",       "Cortex-A53",        false, false, true);
    add(0x0562, 0x8A00, "MT6757",  "Helio P20",       "Cortex-A53",        true,  false, true);
    add(0x0571, 0x8A00, "MT6799",  "Helio X30",       "Cortex-A73+A53",    true,  false, true);
    add(0x0588, 0x8A00, "MT6763",  "Helio P23",       "Cortex-A53",        true,  false, true);
    add(0x0690, 0x8A00, "MT6763V", "Helio P23",       "Cortex-A53",        true,  false, true);
    add(0x0699, 0x8A00, "MT6739",  "MT6739",          "Cortex-A53",        false, false, true);
    add(0x0707, 0x8A00, "MT6768",  "Helio G85",       "Cortex-A75+A55",    true,  true,  true);
    add(0x0717, 0x8A00, "MT6761",  "Helio A20",       "Cortex-A53",        true,  true,  true);
    add(0x0725, 0x8A00, "MT8168",  "MT8168",          "Cortex-A53",        true,  false, false);
    add(0x0766, 0x8A00, "MT6765",  "Helio P35",       "Cortex-A53",        true,  true,  true);
    add(0x0788, 0x8A00, "MT6771",  "Helio P60",       "Cortex-A73+A53",    true,  true,  true);
    add(0x0793, 0x8A00, "MT6779",  "Helio P90",       "Cortex-A75+A55",    true,  true,  true);
    add(0x0813, 0x8A00, "MT6785",  "Helio G90",       "Cortex-A76+A55",    true,  true,  true);
    add(0x0886, 0x8A00, "MT6833",  "Dimensity 700",   "Cortex-A76+A55",    true,  true,  false);
    add(0x0950, 0x8A00, "MT6853",  "Dimensity 720",   "Cortex-A76+A55",    true,  true,  false);
    add(0x0959, 0x8A00, "MT6873",  "Dimensity 800",   "Cortex-A76+A55",    true,  true,  false);
    add(0x0996, 0x8A00, "MT6893",  "Dimensity 1200",  "Cortex-A78+A55",    true,  true,  false);
    add(0x0816, 0x8A00, "MT6885",  "Dimensity 1000+", "Cortex-A77+A55",    true,  true,  false);
    add(0x0975, 0x8A00, "MT6983",  "Dimensity 9000",  "Cortex-X2+A710+A510", true, true, false);
    add(0x0985, 0x8A00, "MT6895",  "Dimensity 8100",  "Cortex-A78+A55",    true,  true,  false);
    add(0x0990, 0x8A00, "MT6789",  "Helio G99",       "Cortex-A76+A55",    true,  true,  false);

    // Tablet / IoT SoCs
    add(0x0507, 0x8A00, "MT8127",  "MT8127",          "Cortex-A7",         false, false, true);
    add(0x0562, 0x8B00, "MT8173",  "MT8173",          "Cortex-A72+A53",    true,  false, false);
    add(0x0690, 0x8B00, "MT8183",  "MT8183",          "Cortex-A73+A53",    true,  true,  false);
}

} // namespace sakura
