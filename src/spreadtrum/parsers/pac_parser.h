#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

namespace sakura {

// ── PAC file entry ──────────────────────────────────────────────────────────

struct PacFileEntry {
    QString  partitionName;        // Target partition (e.g., "boot", "system")
    QString  fileName;             // File name within the PAC
    uint64_t dataOffset = 0;       // Offset of data within the PAC file
    uint64_t size = 0;             // Size of file data
    uint32_t address = 0;          // Target flash address
    bool     usePartName = false;  // Write to partition by name vs address
    uint32_t flags = 0;            // Operation flags

    bool isValid() const { return !fileName.isEmpty() && size > 0; }
    QString sizeHuman() const {
        if (size >= (1ULL << 30))
            return QString("%1 GB").arg(size / double(1ULL << 30), 0, 'f', 2);
        if (size >= (1ULL << 20))
            return QString("%1 MB").arg(size / double(1ULL << 20), 0, 'f', 2);
        return QString("%1 KB").arg(size / double(1ULL << 10), 0, 'f', 1);
    }
};

// ── PAC file header (on-disk, little-endian Unicode strings) ────────────────

#pragma pack(push, 1)
struct PacFileHeader {
    uint32_t version;
    uint32_t headerSize;
    uint32_t productNameOffset;    // UTF-16LE string offset
    uint32_t productNameLength;
    uint32_t firmwareNameOffset;
    uint32_t firmwareNameLength;
    uint32_t partitionCount;
    uint32_t partitionTableOffset;
    uint32_t partitionTableSize;
    uint32_t reserved[5];
    uint32_t crc32;
};

struct PacPartitionHeader {
    uint32_t partitionSize;        // Size of this header
    wchar_t  partitionName[256];   // UTF-16LE partition name
    wchar_t  fileName[256];        // UTF-16LE file name
    uint32_t dataSize;             // Size of partition data
    uint32_t dataSizeHi;           // High 32 bits (for >4GB files)
    uint32_t fileOffset;           // Offset in PAC file
    uint32_t fileOffsetHi;         // High 32 bits
    uint32_t address;              // Target address
    uint32_t addressHi;
    uint32_t flags;
    uint32_t reserved[5];
};
#pragma pack(pop)

// ── PAC info aggregate ──────────────────────────────────────────────────────

struct PacInfo {
    PacFileHeader header;
    QString productName;
    QString firmwareName;
    QList<PacFileEntry> files;
    QString xmlConfig;             // Embedded XML partition config (if present)
};

// ── PAC parser ──────────────────────────────────────────────────────────────

class PacParser {
public:
    PacParser() = default;

    // Parse a PAC file from disk
    bool parse(const QString& filePath);

    // Access parsed data
    QList<PacFileEntry> getFiles() const { return m_info.files; }
    QList<PacFileEntry> getPartitions() const;  // Only entries targeting partitions
    PacInfo pacInfo() const { return m_info; }
    int fileCount() const { return m_info.files.size(); }

    // Read file data for a specific entry from the PAC
    QByteArray readFileData(const PacFileEntry& entry) const;

    // Error info
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }

    // Utilities
    static bool isPacFile(const QString& filePath);

private:
    bool parseHeader(const QByteArray& headerData);
    bool parsePartitionTable(const QByteArray& tableData);
    QString readUtf16String(const wchar_t* data, int maxLen) const;

    PacInfo m_info;
    QString m_filePath;
    bool m_valid = false;
    QString m_error;
};

} // namespace sakura
