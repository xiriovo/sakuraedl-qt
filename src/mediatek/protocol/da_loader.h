#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

namespace sakura {

// ── DA (Download Agent) file format structures ──────────────────────────────

enum class DaType : uint8_t {
    DA1 = 0,    // First-stage DA (loaded via BROM)
    DA2 = 1     // Second-stage DA (loaded by DA1)
};

struct DaEntry {
    QString   name;
    uint16_t  hwCode = 0;
    uint16_t  hwSubCode = 0;
    uint32_t  loadAddr = 0;
    uint32_t  entryAddr = 0;
    uint32_t  signatureLen = 0;
    DaType    daType = DaType::DA1;
    QByteArray data;            // Raw DA binary
    QByteArray signature;       // Optional signature

    bool isValid() const { return !data.isEmpty() && loadAddr != 0; }
    uint32_t totalSize() const { return static_cast<uint32_t>(data.size()); }
};

// ── DA file header (on-disk format) ─────────────────────────────────────────

#pragma pack(push, 1)
struct DaFileHeader {
    char     magic[4];          // "MTK\0"
    uint32_t version;
    uint32_t entryCount;
    uint32_t reserved[5];
};

struct DaEntryHeader {
    char     name[64];
    uint32_t hwCode;
    uint32_t hwSubCode;
    uint32_t hwVersion;
    uint32_t swVersion;
    uint32_t loadAddr;
    uint32_t entryAddr;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t signatureLen;
    uint32_t type;              // 0=DA1, 1=DA2
    uint32_t reserved[4];
};
#pragma pack(pop)

// ── DA loader: parses MTK DA files ──────────────────────────────────────────

class DaLoader {
public:
    DaLoader() = default;

    // Parse a DA file from raw bytes
    bool parseDaFile(const QByteArray& fileData);

    // Parse from file path
    bool parseDaFile(const QString& filePath);

    // Retrieve DA entries
    DaEntry getDa1() const;
    DaEntry getDa2() const;
    QList<DaEntry> getAllEntries() const { return m_entries; }

    // Find DA entry matching a specific HW code
    DaEntry findDa1ForHwCode(uint16_t hwCode) const;
    DaEntry findDa2ForHwCode(uint16_t hwCode) const;

    // Info
    int entryCount() const { return m_entries.size(); }
    bool isValid() const { return !m_entries.isEmpty(); }
    QString errorString() const { return m_error; }

private:
    bool parseEntryHeaders(const QByteArray& fileData);
    bool extractEntryData(const QByteArray& fileData, const DaEntryHeader& hdr,
                          DaEntry& entry);

    QList<DaEntry> m_entries;
    uint32_t m_version = 0;
    QString m_error;
};

} // namespace sakura
