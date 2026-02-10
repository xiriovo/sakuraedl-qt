#include "provision_service.h"
#include "qualcomm/protocol/firehose_client.h"
#include "core/logger.h"

#include <QXmlStreamWriter>

static const QString TAG = QStringLiteral("Provision");

namespace sakura {

ProvisionService::ProvisionService(QObject* parent)
    : QObject(parent)
{
}

// ─── Generate UFS provision XML ──────────────────────────────────────

QString ProvisionService::generateUfsProvisionXml(const QList<UfsLunConfig>& luns)
{
    if (luns.isEmpty() || luns.size() > static_cast<int>(MAX_UFS_LUNS)) {
        LOG_ERROR_CAT(TAG, "Invalid LUN count");
        return {};
    }

    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");

    // UFS provision command
    w.writeStartElement("ufs");
    w.writeAttribute("bNumberLU", QString::number(luns.size()));
    w.writeAttribute("bBootEnable", "1");
    w.writeAttribute("bDescrAccessEn", "1");
    w.writeAttribute("bInitPowerMode", "1");
    w.writeAttribute("bHighPriorityLUN", "0");
    w.writeAttribute("bSecureRemovalType", "0");
    w.writeAttribute("bInitActiveICCLevel", "0");
    w.writeAttribute("wPeriodicRTCUpdate", "0");
    w.writeAttribute("bConfigDescrLock", "0"); // Keep unlocked during provisioning

    // Individual LUN configuration
    for (const auto& lun : luns) {
        w.writeStartElement("lun");
        w.writeAttribute("LUNum", QString::number(lun.lunNumber));
        w.writeAttribute("bLUEnable", "1");
        w.writeAttribute("bBootLunID", lun.bootable ? "1" : "0");
        w.writeAttribute("bLUWriteProtect", lun.writeProtect ? "1" : "0");
        w.writeAttribute("bMemoryType", QString::number(lun.memoryType));
        w.writeAttribute("size_in_KB", QString::number(lun.capacity / 1024));
        w.writeAttribute("bDataReliability", "1");
        w.writeAttribute("bLogicalBlockSize", QString::number(lun.logicalBlockSize));
        w.writeAttribute("bProvisioningType", "3"); // TPRZ
        w.writeAttribute("wContextCapabilities", "0");

        if (!lun.desc.isEmpty())
            w.writeAttribute("desc", lun.desc);

        w.writeEndElement(); // lun
    }

    w.writeEndElement(); // ufs
    w.writeEndElement(); // data
    w.writeEndDocument();

    return xml;
}

// ─── Generate eMMC provision XML ─────────────────────────────────────

QString ProvisionService::generateEmmcProvisionXml(const EmmcProvisionConfig& config)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("data");

    w.writeStartElement("emmc");
    w.writeAttribute("SECTOR_SIZE_IN_BYTES", "512");

    if (config.enhancedAreaSize > 0) {
        w.writeAttribute("enhanced_area_start", QString::number(config.enhancedAreaStart));
        w.writeAttribute("enhanced_area_size", QString::number(config.enhancedAreaSize));
    }

    for (int i = 0; i < 4; ++i) {
        if (config.gpPartitionSize[i] > 0) {
            w.writeAttribute(QString("gp_partition%1_size").arg(i + 1),
                            QString::number(config.gpPartitionSize[i]));
        }
    }

    if (config.reliableWrite) {
        w.writeAttribute("reliable_write", "1");
    }

    w.writeEndElement(); // emmc
    w.writeEndElement(); // data
    w.writeEndDocument();

    return xml;
}

// ─── Apply provision XML via Firehose ────────────────────────────────

bool ProvisionService::applyProvisionXml(FirehoseClient* client, const QString& xml)
{
    if (!client) {
        emit errorOccurred("No Firehose client");
        return false;
    }

    LOG_INFO_CAT(TAG, "Applying provisioning configuration");
    emit statusMessage("Applying provision configuration...");

    auto resp = client->sendRawXml(xml);
    if (!resp.success) {
        QString err = QString("Provisioning failed: %1").arg(resp.rawValue);
        LOG_ERROR_CAT(TAG, err);
        emit errorOccurred(err);
        return false;
    }

    LOG_INFO_CAT(TAG, "Provisioning applied successfully");
    emit statusMessage("Provisioning complete");
    return true;
}

// ─── Provision UFS ───────────────────────────────────────────────────

bool ProvisionService::provisionUfs(FirehoseClient* client, const QList<UfsLunConfig>& luns)
{
    QString xml = generateUfsProvisionXml(luns);
    if (xml.isEmpty())
        return false;

    return applyProvisionXml(client, xml);
}

// ─── Provision eMMC ──────────────────────────────────────────────────

bool ProvisionService::provisionEmmc(FirehoseClient* client, const EmmcProvisionConfig& config)
{
    QString xml = generateEmmcProvisionXml(config);
    if (xml.isEmpty())
        return false;

    return applyProvisionXml(client, xml);
}

// ─── Read current UFS config ─────────────────────────────────────────

QList<UfsLunConfig> ProvisionService::readCurrentUfsConfig(FirehoseClient* client)
{
    if (!client) return {};

    // Send getstorageinfo command
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" ?>"
        "<data><getstorageinfo physical_partition_number=\"0\" /></data>");

    auto resp = client->sendRawXml(xml);
    if (!resp.success || resp.rawXml.isEmpty()) {
        LOG_WARNING_CAT(TAG, "getstorageinfo command failed");
        return {};
    }

    // Parse the Firehose XML response for UFS LUN configuration
    // Response format:
    // <data><storage_info ... num_physical=N .../>
    //   <lun index=0 capacity=... type=... />
    //   ...
    // </data>
    QList<UfsLunConfig> luns;
    QXmlStreamReader reader(resp.rawXml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;

        if (reader.name() == QStringLiteral("lun") ||
            reader.name() == QStringLiteral("LUN")) {
            UfsLunConfig lun;
            auto attrs = reader.attributes();
            lun.lunNumber = attrs.value("index").toUInt();
            if (attrs.hasAttribute("LUNum"))
                lun.lunNumber = attrs.value("LUNum").toUInt();
            lun.capacity = attrs.value("capacity").toULongLong();
            if (attrs.hasAttribute("size_in_KB"))
                lun.capacity = attrs.value("size_in_KB").toULongLong() * 1024;
            lun.memoryType = attrs.value("bMemoryType").toUInt();
            lun.bootable = attrs.value("bBootLunID").toString() != "0";
            lun.writeProtect = attrs.value("bLUWriteProtect").toString() != "0";
            lun.logicalBlockSize = attrs.value("bLogicalBlockSize").toUInt();
            if (lun.logicalBlockSize == 0) lun.logicalBlockSize = 4096;
            lun.desc = attrs.value("desc").toString();
            luns.append(lun);
        } else if (reader.name() == QStringLiteral("storage_info")) {
            auto attrs = reader.attributes();
            QString numPhys = attrs.value("num_physical").toString();
            if (!numPhys.isEmpty()) {
                LOG_INFO_CAT(TAG, QString("UFS has %1 physical partitions").arg(numPhys));
            }
        }
    }

    LOG_INFO_CAT(TAG, QString("Read %1 UFS LUN configurations").arg(luns.size()));
    return luns;
}

// ─── Default layouts ─────────────────────────────────────────────────

QList<UfsLunConfig> ProvisionService::defaultUfsLayout6Lun()
{
    // Standard 6-LUN layout used by most Qualcomm devices
    return {
        {0, 0,          "Boot A (xbl, abl)",  true,  false, 1, 4096},
        {1, 0,          "Boot B (xbl, abl)",  true,  false, 1, 4096},
        {2, 0,          "Boot backup",         false, false, 1, 4096},
        {3, 0,          "GPT / misc",          false, false, 0, 4096},
        {4, 0,          "Userdata",            false, false, 0, 4096},
        {5, 0,          "RPMB / system",       false, false, 0, 4096},
    };
}

QList<UfsLunConfig> ProvisionService::defaultUfsLayout5Lun()
{
    return {
        {0, 0,          "Boot A (xbl, abl)",  true,  false, 1, 4096},
        {1, 0,          "Boot B (xbl, abl)",  true,  false, 1, 4096},
        {2, 0,          "Boot backup",         false, false, 1, 4096},
        {3, 0,          "GPT / misc",          false, false, 0, 4096},
        {4, 0,          "Userdata",            false, false, 0, 4096},
    };
}

} // namespace sakura
