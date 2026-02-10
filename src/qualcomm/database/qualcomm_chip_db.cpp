#include "qualcomm_chip_db.h"

namespace sakura {

QMap<uint32_t, QualcommChipInfo> QualcommChipDb::s_database;
bool QualcommChipDb::s_initialized = false;

void QualcommChipDb::initDb()
{
    if (s_initialized)
        return;

    // Populate with known Qualcomm chipsets
    // MSM ID is the value read from Sahara MsmHwIdRead upper 16 bits (or full 32)

    auto add = [](uint32_t msm, const QString& name, const QString& code,
                  const QString& series, uint32_t jtag = 0) {
        QualcommChipInfo info;
        info.msmId = msm;
        info.name = name;
        info.codeName = code;
        info.series = series;
        info.jtagId = jtag;
        s_database.insert(msm, info);
    };

    // ── Snapdragon 8xx series ────────────────────────────────────────
    add(0x009440E1, "SDM845",  "sdm845",   "Snapdragon 845",  0x000CC0E1);
    add(0x009270E1, "SDM835",  "msm8998",  "Snapdragon 835",  0x000BA0E1);
    add(0x007050E1, "MSM8996", "msm8996",  "Snapdragon 820",  0x000940E1);
    add(0x009900E1, "SM8150",  "msmnile",  "Snapdragon 855",  0x000E60E1);
    add(0x009B00E1, "SM8250",  "kona",     "Snapdragon 865",  0x000F10E1);
    add(0x00B600E1, "SM8350",  "lahaina",  "Snapdragon 888",  0x001220E1);
    add(0x00BD0001, "SM8450",  "waipio",   "Snapdragon 8 Gen 1");
    add(0x00C80001, "SM8550",  "kalama",   "Snapdragon 8 Gen 2");
    add(0x00D50001, "SM8650",  "pineapple","Snapdragon 8 Gen 3");

    // ── Snapdragon 7xx series ────────────────────────────────────────
    add(0x009D00E1, "SM7150",  "sdmmagpie","Snapdragon 730/G");
    add(0x009E00E1, "SM7250",  "lito",     "Snapdragon 765/G");
    add(0x00B300E1, "SM7325",  "yupik",    "Snapdragon 778G");
    add(0x00BB0001, "SM7350",  "kodiak",   "Snapdragon 7 Gen 1");
    add(0x00C50001, "SM7450",  "palima",   "Snapdragon 7+ Gen 2");

    // ── Snapdragon 6xx series ────────────────────────────────────────
    add(0x009500E1, "SDM660",  "sdm660",   "Snapdragon 660");
    add(0x009A00E1, "SM6150",  "talos",    "Snapdragon 675");
    add(0x00AC00E1, "SM6250",  "atoll",    "Snapdragon 690");
    add(0x00B000E1, "SM6350",  "lagoon",   "Snapdragon 690");
    add(0x00B500E1, "SM6375",  "blair",    "Snapdragon 695");
    add(0x00C20001, "SM6450",  "parrot",   "Snapdragon 6 Gen 1");

    // ── Snapdragon 4xx series ────────────────────────────────────────
    add(0x009600E1, "SDM450",  "sdm450",   "Snapdragon 450");
    add(0x009000E1, "MSM8953", "msm8953",  "Snapdragon 625");
    add(0x009100E1, "MSM8937", "msm8937",  "Snapdragon 430");
    add(0x009200E1, "MSM8917", "msm8917",  "Snapdragon 425");
    add(0x00B100E1, "SM4350",  "holi",     "Snapdragon 480");

    // ── Snapdragon 2xx series ────────────────────────────────────────
    add(0x008C00E1, "MSM8909", "msm8909",  "Snapdragon 210");
    add(0x009300E1, "QM215",   "qm215",    "Snapdragon 215");

    // ── MediaTek-rebrand / custom ────────────────────────────────────
    // (Some Qualcomm-adjacent platforms)
    add(0x000860E1, "MDM9607", "mdm9607",  "MDM9607 (IoT)");
    add(0x000790E1, "MDM9650", "mdm9650",  "MDM9650 (Modem)");

    s_initialized = true;
}

QualcommChipInfo QualcommChipDb::lookup(uint32_t msmId)
{
    initDb();

    auto it = s_database.find(msmId);
    if (it != s_database.end())
        return it.value();

    // Try matching upper 16 bits only (some devices report differently)
    uint32_t upper = msmId & 0xFFFF0000;
    for (auto jt = s_database.begin(); jt != s_database.end(); ++jt) {
        if ((jt.key() & 0xFFFF0000) == upper)
            return jt.value();
    }

    // Unknown chip
    QualcommChipInfo unknown;
    unknown.msmId = msmId;
    unknown.name = QString("Unknown (0x%1)").arg(msmId, 8, 16, QChar('0'));
    return unknown;
}

QualcommChipInfo QualcommChipDb::lookupByName(const QString& name)
{
    initDb();

    QString lower = name.toLower();
    for (auto it = s_database.begin(); it != s_database.end(); ++it) {
        if (it.value().name.toLower() == lower ||
            it.value().codeName.toLower() == lower) {
            return it.value();
        }
    }

    return {};
}

QList<QualcommChipInfo> QualcommChipDb::allChips()
{
    initDb();
    return s_database.values();
}

bool QualcommChipDb::isKnown(uint32_t msmId)
{
    initDb();
    return s_database.contains(msmId);
}

QString QualcommChipDb::chipNameForMsm(uint32_t msmId)
{
    auto info = lookup(msmId);
    return info.name;
}

} // namespace sakura
