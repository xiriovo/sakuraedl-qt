#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

namespace sakura {

// ─── Motorola firmware package structures ────────────────────────────

// Motorola uses .xml flash files and sometimes custom MBN headers
struct MotoFlashEntry {
    QString  operation;          // "program", "erase", "patch", "getsha256digest"
    QString  partition;
    QString  filename;
    QString  md5;
    uint64_t startSector = 0;
    uint32_t physicalPartition = 0;
    bool     sparse = false;
};

struct MotoFlashManifest {
    QString  version;
    QString  device;
    QString  chipset;
    QList<MotoFlashEntry> entries;
    bool     valid = false;
    QString  errorMessage;
};

// Motorola bootloader unlock token
struct MotoUnlockToken {
    QByteArray data;
    QString    deviceId;
    bool       valid = false;
};

// ─── Motorola firmware support ───────────────────────────────────────
class MotorolaSupport {
public:
    // Parse Motorola flashfile.xml manifest
    static MotoFlashManifest parseFlashManifest(const QString& xmlPath);
    static MotoFlashManifest parseFlashManifestXml(const QByteArray& xmlData);

    // Parse Motorola service file (.lst or .xml)
    static QList<MotoFlashEntry> parseServiceFile(const QString& path);

    // Check if a directory is a Motorola firmware package
    static bool isMotoPackage(const QString& directory);

    // Find the flashfile.xml in a Motorola package
    static QString findFlashManifest(const QString& directory);

    // Generate command sequence from manifest
    static QStringList generateFlashSequence(const MotoFlashManifest& manifest);

    // Motorola-specific partition name mapping
    static QString normalizePartitionName(const QString& motoName);

    // Detect if a file is a Motorola MBN/SBL binary
    static bool isMotoMbn(const QByteArray& data);

    // Read unlock status from bootloader
    // TODO: Requires Fastboot, not EDL — placeholder
    static MotoUnlockToken readUnlockToken(const QByteArray& response);

private:
    static const QMap<QString, QString>& partitionNameMap();
};

} // namespace sakura
