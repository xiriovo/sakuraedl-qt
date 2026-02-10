#include "loader_feature_detector.h"
#include "core/logger.h"

#include <QRegularExpression>
#include <cstring>

static const QString TAG = QStringLiteral("LoaderDetector");

namespace sakura {

// ─── ELF header constants ────────────────────────────────────────────
static constexpr uint8_t ELF_MAGIC[] = { 0x7F, 'E', 'L', 'F' };

bool LoaderFeatureDetector::isElf(const QByteArray& data)
{
    return data.size() >= 16 && std::memcmp(data.constData(), ELF_MAGIC, 4) == 0;
}

bool LoaderFeatureDetector::isFirehoseLoader(const QByteArray& data)
{
    if (!isElf(data))
        return false;

    // Look for Firehose-specific strings
    QStringList strings = extractStrings(data, 8);
    return containsString(strings, "firehose") ||
           containsString(strings, "Firehose") ||
           containsString(strings, "FIREHOSE") ||
           containsString(strings, "<configure") ||
           containsString(strings, "MaxPayloadSizeToTargetInBytes");
}

// ─── String extraction ───────────────────────────────────────────────

QStringList LoaderFeatureDetector::extractStrings(const QByteArray& data, int minLength)
{
    QStringList result;
    QString current;

    for (int i = 0; i < data.size(); ++i) {
        char c = data[i];
        if (c >= 0x20 && c < 0x7F) {
            current += QChar(c);
        } else {
            if (current.length() >= minLength) {
                result.append(current);
            }
            current.clear();
        }
    }

    if (current.length() >= minLength)
        result.append(current);

    return result;
}

bool LoaderFeatureDetector::containsString(const QStringList& strings, const QString& pattern)
{
    for (const auto& s : strings) {
        if (s.contains(pattern, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// ─── Feature detection ───────────────────────────────────────────────

LoaderFeatures LoaderFeatureDetector::detect(const QByteArray& loaderBinary)
{
    LoaderFeatures features;

    if (loaderBinary.isEmpty()) {
        LOG_ERROR_CAT(TAG, "Empty loader binary");
        return features;
    }

    if (!isElf(loaderBinary)) {
        LOG_WARNING_CAT(TAG, "Not an ELF binary — limited detection");
    }

    QStringList strings = extractStrings(loaderBinary, 4);
    features.strings = strings;
    features.valid = true;

    // ── Detect protocol features from embedded XML command strings ────
    features.supportsRead    = containsString(strings, "<read ");
    features.supportsProgram = containsString(strings, "<program ");
    features.supportsErase   = containsString(strings, "<erase ");
    features.supportsPeek    = containsString(strings, "<peek ");
    features.supportsPoke    = containsString(strings, "<poke ");
    features.supportsGetGpt  = containsString(strings, "getgpt") ||
                               containsString(strings, "read SECTOR_SIZE_IN_BYTES");
    features.supportsSetActiveSlot = containsString(strings, "setactiveslot") ||
                                      containsString(strings, "SetActiveSlot");
    features.supportsPatch   = containsString(strings, "<patch ");
    features.supportsNop     = containsString(strings, "<nop");
    features.supportsProvision = containsString(strings, "<ufs") ||
                                  containsString(strings, "provision");

    // ── Detect storage support ───────────────────────────────────────
    features.supportsUfs  = containsString(strings, "UFS") || containsString(strings, "ufs");
    features.supportsEmmc = containsString(strings, "emmc") || containsString(strings, "eMMC");
    features.supportsNand = containsString(strings, "nand") || containsString(strings, "NAND");

    // ── Detect auth requirements ─────────────────────────────────────
    features.requiresAuth   = containsString(strings, "sig size_in_bytes") ||
                              containsString(strings, "OemPkHash") ||
                              containsString(strings, "Token=") ||
                              containsString(strings, "authenticate");
    features.hasHashChecking = containsString(strings, "hash_check") ||
                               containsString(strings, "digest");

    // Detect vendor
    features.authVendor = detectVendor(loaderBinary);

    // ── Extract max payload size ─────────────────────────────────────
    features.maxPayloadSize = detectMaxPayload(loaderBinary);

    // ── Extract build info ───────────────────────────────────────────
    for (const auto& s : strings) {
        static QRegularExpression dateRe(R"(\b(20\d{2}[-/]\d{2}[-/]\d{2})\b)");
        auto match = dateRe.match(s);
        if (match.hasMatch() && features.buildDate.isEmpty()) {
            features.buildDate = match.captured(1);
        }
        static QRegularExpression verRe(R"(\bQ[A-Z]+-[A-Z0-9]+-\d+\b)");
        auto verMatch = verRe.match(s);
        if (verMatch.hasMatch() && features.buildVersion.isEmpty()) {
            features.buildVersion = verMatch.captured(0);
        }
    }

    LOG_INFO_CAT(TAG, QString("Loader features: read=%1 program=%2 erase=%3 peek=%4 auth=%5 vendor=%6")
                    .arg(features.supportsRead).arg(features.supportsProgram)
                    .arg(features.supportsErase).arg(features.supportsPeek)
                    .arg(features.requiresAuth).arg(features.authVendor));

    return features;
}

// ─── Detect max payload ──────────────────────────────────────────────

uint32_t LoaderFeatureDetector::detectMaxPayload(const QByteArray& data)
{
    // Look for common max payload sizes embedded in the binary
    // Typical values: 0x100000 (1MB), 0x40000 (256KB), 0x80000 (512KB)

    QStringList strings = extractStrings(data, 4);

    for (const auto& s : strings) {
        if (s.contains("MaxPayloadSizeToTargetInBytes", Qt::CaseInsensitive)) {
            // Try to extract the number following the attribute
            static QRegularExpression re(R"(MaxPayloadSizeToTargetInBytes[\"=\s]+(\d+))");
            auto match = re.match(s);
            if (match.hasMatch()) {
                return match.captured(1).toUInt();
            }
        }
    }

    // Default to 1MB
    return 1048576;
}

// ─── Detect vendor ───────────────────────────────────────────────────

QString LoaderFeatureDetector::detectVendor(const QByteArray& data)
{
    QStringList strings = extractStrings(data, 4);

    if (containsString(strings, "OnePlus") || containsString(strings, "oneplus"))
        return QStringLiteral("oneplus");
    if (containsString(strings, "Xiaomi") || containsString(strings, "xiaomi") ||
        containsString(strings, "miui"))
        return QStringLiteral("xiaomi");
    if (containsString(strings, "Samsung") || containsString(strings, "samsung"))
        return QStringLiteral("samsung");
    if (containsString(strings, "Motorola") || containsString(strings, "motorola"))
        return QStringLiteral("motorola");
    if (containsString(strings, "OPPO") || containsString(strings, "oppo"))
        return QStringLiteral("oppo");
    if (containsString(strings, "Realme") || containsString(strings, "realme"))
        return QStringLiteral("realme");
    if (containsString(strings, "vivo") || containsString(strings, "VIVO"))
        return QStringLiteral("vivo");
    if (containsString(strings, "Google") || containsString(strings, "Pixel"))
        return QStringLiteral("google");

    return QStringLiteral("generic");
}

// ─── ELF segment extraction ─────────────────────────────────────────

QByteArray LoaderFeatureDetector::extractElfSegments(const QByteArray& data)
{
    // Parse ELF headers to extract PT_LOAD segments (loadable code/data)
    // These segments contain the actual Firehose code for analysis.
    if (!isElf(data) || data.size() < 64)
        return data;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // Detect 32-bit vs 64-bit ELF
    bool is64 = (d[4] == 2); // EI_CLASS: 1=32bit, 2=64bit

    QByteArray segments;

    if (is64) {
        // ELF64 header: e_phoff at offset 32 (8 bytes)
        //               e_phentsize at 54 (2 bytes), e_phnum at 56 (2 bytes)
        if (data.size() < 64) return data;
        uint64_t phOff;   std::memcpy(&phOff,   d + 32, 8);
        uint16_t phSize;  std::memcpy(&phSize,  d + 54, 2);
        uint16_t phNum;   std::memcpy(&phNum,   d + 56, 2);

        for (uint16_t i = 0; i < phNum; ++i) {
            uint64_t entryOff = phOff + static_cast<uint64_t>(i) * phSize;
            if (entryOff + 56 > static_cast<uint64_t>(data.size())) break;

            const uint8_t* ph = d + entryOff;
            uint32_t pType;   std::memcpy(&pType, ph, 4);
            if (pType != 1) continue; // PT_LOAD = 1

            uint64_t offset;  std::memcpy(&offset, ph + 8, 8);
            uint64_t filesz;  std::memcpy(&filesz, ph + 32, 8);

            if (offset + filesz <= static_cast<uint64_t>(data.size()) && filesz > 0) {
                segments.append(data.mid(static_cast<int>(offset), static_cast<int>(filesz)));
            }
        }
    } else {
        // ELF32 header: e_phoff at offset 28 (4 bytes)
        //               e_phentsize at 42 (2 bytes), e_phnum at 44 (2 bytes)
        if (data.size() < 52) return data;
        uint32_t phOff;   std::memcpy(&phOff,   d + 28, 4);
        uint16_t phSize;  std::memcpy(&phSize,  d + 42, 2);
        uint16_t phNum;   std::memcpy(&phNum,   d + 44, 2);

        for (uint16_t i = 0; i < phNum; ++i) {
            uint32_t entryOff = phOff + static_cast<uint32_t>(i) * phSize;
            if (entryOff + 32 > static_cast<uint32_t>(data.size())) break;

            const uint8_t* ph = d + entryOff;
            uint32_t pType;   std::memcpy(&pType, ph, 4);
            if (pType != 1) continue; // PT_LOAD = 1

            uint32_t offset;  std::memcpy(&offset, ph + 4, 4);
            uint32_t filesz;  std::memcpy(&filesz, ph + 16, 4);

            if (offset + filesz <= static_cast<uint32_t>(data.size()) && filesz > 0) {
                segments.append(data.mid(static_cast<int>(offset), static_cast<int>(filesz)));
            }
        }
    }

    if (segments.isEmpty()) {
        LOG_WARNING_CAT(TAG, "No PT_LOAD segments found, using full binary");
        return data;
    }

    LOG_INFO_CAT(TAG, QString("Extracted %1 bytes of loadable segments").arg(segments.size()));
    return segments;
}

} // namespace sakura
