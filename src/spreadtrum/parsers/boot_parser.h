#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace sakura {

// ── Android boot image format ───────────────────────────────────────────────

constexpr uint32_t BOOT_MAGIC_SIZE = 8;
constexpr char     BOOT_MAGIC[]    = "ANDROID!";

#pragma pack(push, 1)
struct BootImageHeader {
    char     magic[BOOT_MAGIC_SIZE];
    uint32_t kernelSize;
    uint32_t kernelAddr;
    uint32_t ramdiskSize;
    uint32_t ramdiskAddr;
    uint32_t secondSize;
    uint32_t secondAddr;
    uint32_t tagsAddr;
    uint32_t pageSize;
    uint32_t headerVersion;        // 0, 1, 2, 3, or 4
    uint32_t osVersion;
    char     name[16];
    char     cmdline[512];
    uint32_t id[8];                // SHA digest
    char     extraCmdline[1024];
};

// Header V1 additions
struct BootImageHeaderV1Extra {
    uint32_t recoveryDtboSize;
    uint64_t recoveryDtboOffset;
    uint32_t headerSize;
};

// Header V2 additions
struct BootImageHeaderV2Extra {
    uint32_t dtbSize;
    uint64_t dtbAddr;
};

// Header V3 (simplified — no second stage)
struct BootImageHeaderV3 {
    char     magic[BOOT_MAGIC_SIZE];
    uint32_t kernelSize;
    uint32_t ramdiskSize;
    uint32_t osVersion;
    uint32_t headerSize;
    uint32_t reserved[4];
    uint32_t headerVersion;
    char     cmdline[1536];
};
#pragma pack(pop)

// ── Parsed boot image components ────────────────────────────────────────────

struct BootImageInfo {
    uint32_t headerVersion = 0;
    uint32_t pageSize = 0;
    QString  name;
    QString  cmdline;

    // Component sizes
    uint32_t kernelSize = 0;
    uint32_t ramdiskSize = 0;
    uint32_t secondSize = 0;
    uint32_t dtbSize = 0;
    uint32_t recoveryDtboSize = 0;

    // Component offsets within the image
    uint64_t kernelOffset = 0;
    uint64_t ramdiskOffset = 0;
    uint64_t secondOffset = 0;
    uint64_t dtbOffset = 0;
    uint64_t recoveryDtboOffset = 0;

    // Load addresses
    uint32_t kernelAddr = 0;
    uint32_t ramdiskAddr = 0;
    uint32_t secondAddr = 0;
    uint32_t tagsAddr = 0;

    bool isValid() const { return pageSize > 0 && kernelSize > 0; }
};

// ── Boot image parser ───────────────────────────────────────────────────────

class BootParser {
public:
    BootParser() = default;

    // Parse a boot.img from raw bytes
    bool parse(const QByteArray& imageData);

    // Parse from file
    bool parseFile(const QString& filePath);

    // Info
    BootImageInfo info() const { return m_info; }
    bool isValid() const { return m_info.isValid(); }
    QString errorString() const { return m_error; }

    // Extract components
    QByteArray extractKernel(const QByteArray& imageData) const;
    QByteArray extractRamdisk(const QByteArray& imageData) const;
    QByteArray extractSecond(const QByteArray& imageData) const;
    QByteArray extractDtb(const QByteArray& imageData) const;

    // Detect boot image
    static bool isBootImage(const QByteArray& data);
    static uint32_t detectHeaderVersion(const QByteArray& data);

private:
    bool parseV0V1V2(const QByteArray& data);
    bool parseV3(const QByteArray& data);
    uint64_t alignToPage(uint64_t offset) const;

    BootImageInfo m_info;
    QString m_error;
};

} // namespace sakura
