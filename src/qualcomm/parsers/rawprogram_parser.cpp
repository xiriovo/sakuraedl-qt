#include "rawprogram_parser.h"
#include "core/logger.h"

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

static const QString TAG = QStringLiteral("RawprogramParser");

namespace sakura {

// ─── Parse rawprogram XML ────────────────────────────────────────────

RawprogramParseResult RawprogramParser::parseRawprogram(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        RawprogramParseResult result;
        result.success = false;
        result.errorMessage = QString("Cannot open file: %1").arg(filePath);
        return result;
    }

    return parseRawprogramXml(file.readAll());
}

RawprogramParseResult RawprogramParser::parseRawprogramXml(const QByteArray& xmlData)
{
    RawprogramParseResult result;
    result.success = true;

    QXmlStreamReader reader(xmlData);

    while (!reader.atEnd()) {
        reader.readNext();

        if (!reader.isStartElement())
            continue;

        if (reader.name() == QStringLiteral("program")) {
            RawprogramEntry entry;
            auto attrs = reader.attributes();

            entry.filename       = attrs.value("filename").toString();
            entry.label          = attrs.value("label").toString();
            entry.startSector    = attrs.value("start_sector").toULongLong();
            entry.numSectors     = attrs.value("num_partition_sectors").toULongLong();
            entry.sectorSize     = attrs.value("SECTOR_SIZE_IN_BYTES").toUInt();
            entry.physicalPartition = attrs.value("physical_partition_number").toUInt();
            entry.readbackVerify = attrs.value("readbackverify").toString() == "true";
            entry.sparse         = attrs.value("sparse").toString() == "true";
            entry.startByteHex   = attrs.value("start_byte_hex").toString();

            // Default sector size if not specified
            if (entry.sectorSize == 0)
                entry.sectorSize = 512;

            // Only include entries with actual data to program
            if (!entry.filename.isEmpty() && entry.filename != "")
                result.programs.append(entry);

        } else if (reader.name() == QStringLiteral("patch")) {
            PatchEntry patch;
            auto attrs = reader.attributes();

            patch.sectorOffset      = attrs.value("sector_offset").toULongLong();
            patch.byteOffset        = attrs.value("byte_offset").toUInt();
            patch.sizeInBytes       = attrs.value("size_in_bytes").toUInt();
            patch.value             = attrs.value("value").toString();
            patch.physicalPartition = attrs.value("physical_partition_number").toUInt();
            patch.filename          = attrs.value("filename").toString();

            result.patches.append(patch);
        }
    }

    if (reader.hasError()) {
        result.success = false;
        result.errorMessage = reader.errorString();
        LOG_ERROR_CAT(TAG, QString("XML parse error: %1").arg(result.errorMessage));
    }

    LOG_INFO_CAT(TAG, QString("Parsed %1 program entries, %2 patches")
                    .arg(result.programs.size()).arg(result.patches.size()));
    return result;
}

// ─── Parse patch XML ─────────────────────────────────────────────────

QList<PatchEntry> RawprogramParser::parsePatch(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open patch file: %1").arg(filePath));
        return {};
    }

    return parsePatchXml(file.readAll());
}

QList<PatchEntry> RawprogramParser::parsePatchXml(const QByteArray& xmlData)
{
    QList<PatchEntry> patches;

    QXmlStreamReader reader(xmlData);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("patch")) {
            PatchEntry patch;
            auto attrs = reader.attributes();

            patch.sectorOffset      = attrs.value("sector_offset").toULongLong();
            patch.byteOffset        = attrs.value("byte_offset").toUInt();
            patch.sizeInBytes       = attrs.value("size_in_bytes").toUInt();
            patch.value             = attrs.value("value").toString();
            patch.physicalPartition = attrs.value("physical_partition_number").toUInt();
            patch.filename          = attrs.value("filename").toString();

            patches.append(patch);
        }
    }

    return patches;
}

// ─── Find files in directory ─────────────────────────────────────────

QStringList RawprogramParser::findRawprogramFiles(const QString& directory)
{
    QDir dir(directory);
    QStringList filters;
    filters << "rawprogram*.xml" << "rawprogram*.XML";
    return dir.entryList(filters, QDir::Files, QDir::Name);
}

QStringList RawprogramParser::findPatchFiles(const QString& directory)
{
    QDir dir(directory);
    QStringList filters;
    filters << "patch*.xml" << "patch*.XML";
    return dir.entryList(filters, QDir::Files, QDir::Name);
}

// ─── Generate XML ────────────────────────────────────────────────────

QString RawprogramParser::generateRawprogramXml(const QList<RawprogramEntry>& entries)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("data");

    for (const auto& e : entries) {
        w.writeStartElement("program");
        w.writeAttribute("SECTOR_SIZE_IN_BYTES", QString::number(e.sectorSize));
        w.writeAttribute("filename", e.filename);
        w.writeAttribute("label", e.label);
        w.writeAttribute("num_partition_sectors", QString::number(e.numSectors));
        w.writeAttribute("physical_partition_number", QString::number(e.physicalPartition));
        w.writeAttribute("start_sector", QString::number(e.startSector));
        if (e.sparse)
            w.writeAttribute("sparse", "true");
        if (e.readbackVerify)
            w.writeAttribute("readbackverify", "true");
        w.writeEndElement();
    }

    w.writeEndElement(); // data
    w.writeEndDocument();
    return xml;
}

QString RawprogramParser::generatePatchXml(const QList<PatchEntry>& patches)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("patches");

    for (const auto& p : patches) {
        w.writeStartElement("patch");
        w.writeAttribute("byte_offset", QString::number(p.byteOffset));
        w.writeAttribute("filename", p.filename);
        w.writeAttribute("physical_partition_number", QString::number(p.physicalPartition));
        w.writeAttribute("sector_offset", QString::number(p.sectorOffset));
        w.writeAttribute("size_in_bytes", QString::number(p.sizeInBytes));
        w.writeAttribute("value", p.value);
        w.writeEndElement();
    }

    w.writeEndElement(); // patches
    w.writeEndDocument();
    return xml;
}

} // namespace sakura
