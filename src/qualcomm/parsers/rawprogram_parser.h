#pragma once

#include <QList>
#include <QString>
#include <cstdint>

namespace sakura {

// ─── Rawprogram XML entry ────────────────────────────────────────────
struct RawprogramEntry {
    QString filename;
    QString label;               // Partition label
    uint64_t startSector = 0;
    uint64_t numSectors = 0;
    uint32_t sectorSize = 0;
    uint32_t physicalPartition = 0; // LUN
    bool     readbackVerify = false;
    bool     sparse = false;      // Sparse image
    QString  startByteHex;        // Original hex for start_byte_hex
};

// ─── Patch XML entry ─────────────────────────────────────────────────
struct PatchEntry {
    uint64_t sectorOffset = 0;
    uint32_t byteOffset = 0;
    uint32_t sizeInBytes = 0;
    QString  value;
    uint32_t physicalPartition = 0;
    QString  filename;             // Usually "DISK"
};

// ─── Rawprogram parse result ─────────────────────────────────────────
struct RawprogramParseResult {
    QList<RawprogramEntry> programs;
    QList<PatchEntry> patches;
    bool success = false;
    QString errorMessage;
};

// ─── Parser for rawprogram*.xml and patch*.xml ───────────────────────
class RawprogramParser {
public:
    // Parse a rawprogram XML file
    static RawprogramParseResult parseRawprogram(const QString& filePath);
    static RawprogramParseResult parseRawprogramXml(const QByteArray& xmlData);

    // Parse a patch XML file
    static QList<PatchEntry> parsePatch(const QString& filePath);
    static QList<PatchEntry> parsePatchXml(const QByteArray& xmlData);

    // Find rawprogram/patch files in a directory
    static QStringList findRawprogramFiles(const QString& directory);
    static QStringList findPatchFiles(const QString& directory);

    // Generate rawprogram XML from entries
    static QString generateRawprogramXml(const QList<RawprogramEntry>& entries);
    static QString generatePatchXml(const QList<PatchEntry>& patches);
};

} // namespace sakura
