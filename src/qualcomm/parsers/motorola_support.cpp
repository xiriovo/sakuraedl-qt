#include "motorola_support.h"
#include "core/logger.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <cstring>

static const QString TAG = QStringLiteral("MotoSupport");

namespace sakura {

// ─── Partition name mapping ──────────────────────────────────────────

const QMap<QString, QString>& MotorolaSupport::partitionNameMap()
{
    static const QMap<QString, QString> map = {
        {"bootloader",  "abl"},
        {"radio",       "modem"},
        {"boot",        "boot"},
        {"system",      "system"},
        {"vendor",      "vendor"},
        {"product",     "product"},
        {"recovery",    "recovery"},
        {"dtbo",        "dtbo"},
        {"vbmeta",      "vbmeta"},
        {"super",       "super"},
        {"userdata",    "userdata"},
        {"metadata",    "metadata"},
        {"fsg",         "fsg"},
        {"logo",        "logo"},
        {"carrier",     "carrier"},
        {"oem",         "oem"},
    };
    return map;
}

QString MotorolaSupport::normalizePartitionName(const QString& motoName)
{
    const auto& map = partitionNameMap();
    QString lower = motoName.toLower();
    auto it = map.find(lower);
    if (it != map.end())
        return it.value();
    return motoName;
}

// ─── Parse flash manifest ────────────────────────────────────────────

MotoFlashManifest MotorolaSupport::parseFlashManifest(const QString& xmlPath)
{
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        MotoFlashManifest result;
        result.valid = false;
        result.errorMessage = QString("Cannot open: %1").arg(xmlPath);
        return result;
    }

    return parseFlashManifestXml(file.readAll());
}

MotoFlashManifest MotorolaSupport::parseFlashManifestXml(const QByteArray& xmlData)
{
    MotoFlashManifest manifest;

    QXmlStreamReader reader(xmlData);

    while (!reader.atEnd()) {
        reader.readNext();

        if (!reader.isStartElement())
            continue;

        QString elemName = reader.name().toString().toLower();

        if (elemName == "phone_model" || elemName == "phone") {
            manifest.device = reader.attributes().value("model").toString();
            if (manifest.device.isEmpty())
                manifest.device = reader.readElementText().trimmed();

        } else if (elemName == "software_version" || elemName == "version") {
            manifest.version = reader.readElementText().trimmed();

        } else if (elemName == "step" || elemName == "command") {
            MotoFlashEntry entry;
            auto attrs = reader.attributes();

            entry.operation = attrs.value("operation").toString().toLower();
            if (entry.operation.isEmpty())
                entry.operation = attrs.value("type").toString().toLower();

            entry.partition = attrs.value("partition").toString();
            if (entry.partition.isEmpty())
                entry.partition = attrs.value("label").toString();

            entry.filename = attrs.value("filename").toString();
            entry.md5 = attrs.value("MD5").toString();

            QString startSector = attrs.value("start_sector").toString();
            if (!startSector.isEmpty())
                entry.startSector = startSector.toULongLong();

            QString physPart = attrs.value("physical_partition_number").toString();
            if (!physPart.isEmpty())
                entry.physicalPartition = physPart.toUInt();

            entry.sparse = attrs.value("sparse").toString().toLower() == "true";

            if (!entry.operation.isEmpty())
                manifest.entries.append(entry);

        } else if (elemName == "chipset") {
            manifest.chipset = reader.readElementText().trimmed();
        }
    }

    if (reader.hasError()) {
        manifest.valid = false;
        manifest.errorMessage = reader.errorString();
        LOG_ERROR_CAT(TAG, QString("Flash manifest parse error: %1").arg(manifest.errorMessage));
    } else {
        manifest.valid = !manifest.entries.isEmpty();
    }

    LOG_INFO_CAT(TAG, QString("Parsed Motorola manifest: device=%1, %2 entries")
                    .arg(manifest.device).arg(manifest.entries.size()));
    return manifest;
}

// ─── Parse service file ──────────────────────────────────────────────

QList<MotoFlashEntry> MotorolaSupport::parseServiceFile(const QString& path)
{
    // Service files (.lst) are simpler: one line per operation
    // Format: operation partition filename [md5]
    QList<MotoFlashEntry> entries;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open service file: %1").arg(path));
        return entries;
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() >= 3) {
            MotoFlashEntry entry;
            entry.operation = parts[0].toLower();
            entry.partition = parts[1];
            entry.filename = parts[2];
            if (parts.size() >= 4)
                entry.md5 = parts[3];
            entries.append(entry);
        }
    }

    return entries;
}

// ─── Package detection ───────────────────────────────────────────────

bool MotorolaSupport::isMotoPackage(const QString& directory)
{
    return !findFlashManifest(directory).isEmpty();
}

QString MotorolaSupport::findFlashManifest(const QString& directory)
{
    QDir dir(directory);

    // Check common Motorola manifest filenames
    QStringList candidates = {
        "flashfile.xml",
        "flashfile_unlocked.xml",
        "servicefile.xml",
        "flash_cmd.xml",
    };

    for (const auto& name : candidates) {
        if (dir.exists(name))
            return dir.filePath(name);
    }

    // Search for any XML that looks like a flash manifest
    QStringList xmlFiles = dir.entryList({"*.xml"}, QDir::Files);
    for (const auto& xmlFile : xmlFiles) {
        QFile file(dir.filePath(xmlFile));
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray header = file.read(1024);
            if (header.contains("phone_model") || header.contains("flash_cmd") ||
                header.contains("<step") || header.contains("operation=\"flash\"")) {
                return dir.filePath(xmlFile);
            }
        }
    }

    return {};
}

// ─── Generate flash sequence ─────────────────────────────────────────

QStringList MotorolaSupport::generateFlashSequence(const MotoFlashManifest& manifest)
{
    QStringList sequence;

    for (const auto& entry : manifest.entries) {
        if (entry.operation == "erase") {
            sequence.append(QString("erase:%1").arg(entry.partition));
        } else if (entry.operation == "flash" || entry.operation == "program") {
            sequence.append(QString("flash:%1:%2").arg(entry.partition, entry.filename));
        } else if (entry.operation == "getsha256digest") {
            sequence.append(QString("verify:%1").arg(entry.partition));
        } else if (entry.operation == "oem") {
            sequence.append(QString("oem:%1").arg(entry.partition));
        }
    }

    return sequence;
}

// ─── MBN detection ───────────────────────────────────────────────────

bool MotorolaSupport::isMotoMbn(const QByteArray& data)
{
    if (data.size() < 40)
        return false;

    // Motorola MBN files have a specific header structure
    // Check for common MBN magic values
    uint32_t magic = 0;
    std::memcpy(&magic, data.constData(), 4);

    // Standard MBN/ELF
    static constexpr uint32_t ELF_MAGIC = 0x464C457F; // "\x7FELF"
    static constexpr uint32_t MBN_MAGIC = 0x00000005;  // SBL header type

    return magic == ELF_MAGIC || magic == MBN_MAGIC;
}

// ─── Unlock token ────────────────────────────────────────────────────

MotoUnlockToken MotorolaSupport::readUnlockToken(const QByteArray& response)
{
    MotoUnlockToken token;

    if (response.isEmpty())
        return token;

    // Motorola unlock tokens are typically returned as hex strings
    // from `fastboot oem get_unlock_data`
    token.data = response;
    token.valid = response.size() >= 32;

    if (token.valid) {
        // Device ID is typically the first 16 bytes
        token.deviceId = QString(response.left(16).toHex());
    }

    return token;
}

} // namespace sakura
