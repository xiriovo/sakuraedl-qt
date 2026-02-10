#include "xml_da_client.h"
#include "transport/i_transport.h"
#include "common/gpt_parser.h"
#include "core/logger.h"

#include <QDomElement>
#include <QDomNodeList>
#include <QMap>
#include <QtEndian>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-XMLDA";

XmlDaClient::XmlDaClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

XmlDaClient::~XmlDaClient() = default;

// ── Initialisation ──────────────────────────────────────────────────────────

bool XmlDaClient::notifyInit()
{
    LOG_INFO_CAT(LOG_TAG, "Sending NOTIFY-INIT...");
    QString xml = buildXmlCommand(XmlDaCmd::CMD_NOTIFY_INIT);

    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

bool XmlDaClient::setFlashPolicy(const QString& policy)
{
    QMap<QString, QString> params;
    params["flash_policy"] = policy;

    QString xml = buildXmlCommand(XmlDaCmd::CMD_SECURITY_SET_FLASH_POLICY, params);

    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

// ── Partition operations ────────────────────────────────────────────────────

QList<PartitionInfo> XmlDaClient::readPartitions()
{
    LOG_INFO_CAT(LOG_TAG, "Requesting GPT via XML DA...");

    QString xml = buildXmlCommand(XmlDaCmd::CMD_GET_GPT);
    if (!sendXml(xml))
        return {};

    QDomDocument resp = recvXmlResponse();
    if (!isResponseOk(resp))
        return {};

    // Read the binary GPT data that follows the XML ack
    QString sizeStr = getResponseField(resp, "length");
    bool sizeOk = false;
    qint64 gptSize = sizeStr.toLongLong(&sizeOk, 0);
    if (!sizeOk || gptSize < 0) {
        LOG_WARNING_CAT(LOG_TAG, QString("Invalid GPT size string: '%1'").arg(sizeStr));
        gptSize = 0;
    }

    QList<PartitionInfo> partitions;

    if (gptSize > 0) {
        // DA sends raw GPT binary after XML response
        QByteArray gptData = recvBinaryPayload(gptSize);
        if (!gptData.isEmpty()) {
            auto result = GptParser::parse(gptData);
            if (result.success) {
                partitions = result.partitions;
                LOG_INFO_CAT(LOG_TAG, QString("Parsed %1 partitions from GPT").arg(partitions.size()));
            } else {
                LOG_ERROR_CAT(LOG_TAG, QString("GPT parse error: %1").arg(result.errorMessage));
            }
        }
    } else {
        // Some DA versions embed partition info inline in XML
        QDomElement root = resp.documentElement();
        QDomNodeList partNodes = root.elementsByTagName("partition");
        for (int i = 0; i < partNodes.size(); ++i) {
            QDomElement pe = partNodes.at(i).toElement();
            PartitionInfo pi;
            pi.name = pe.firstChildElement("name").text();
            pi.startSector = pe.firstChildElement("start").text().toULongLong(nullptr, 0);
            pi.numSectors  = pe.firstChildElement("count").text().toULongLong(nullptr, 0);
            pi.lun = 0;
            partitions.append(pi);
        }
        LOG_INFO_CAT(LOG_TAG, QString("Parsed %1 partitions from XML").arg(partitions.size()));
    }

    return partitions;
}

bool XmlDaClient::writePartition(const QString& name, const QByteArray& data)
{
    LOG_INFO_CAT(LOG_TAG, QString("Writing partition '%1' (%2 bytes) via XML DA")
                              .arg(name).arg(data.size()));

    QMap<QString, QString> params;
    params["partition"]   = name;
    params["offset"]      = "0x0";
    params["length"]      = QString("0x%1").arg(data.size(), 0, 16);

    QString xml = buildXmlCommand(XmlDaCmd::CMD_WRITE_PARTITION, params);
    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    if (!isResponseOk(resp))
        return false;

    // Send binary payload after XML handshake
    return sendBinaryPayload(data);
}

QByteArray XmlDaClient::readPartition(const QString& name, qint64 offset, qint64 length)
{
    LOG_INFO_CAT(LOG_TAG, QString("Reading partition '%1' via XML DA").arg(name));

    QMap<QString, QString> params;
    params["partition"] = name;
    params["offset"]    = QString("0x%1").arg(offset, 0, 16);
    if (length >= 0)
        params["length"] = QString("0x%1").arg(length, 0, 16);

    QString xml = buildXmlCommand(XmlDaCmd::CMD_READ_PARTITION, params);
    if (!sendXml(xml))
        return {};

    QDomDocument resp = recvXmlResponse();
    if (!isResponseOk(resp))
        return {};

    // Determine expected size from response
    QString sizeStr = getResponseField(resp, "length");
    bool sizeOk = false;
    qint64 expectedSize = sizeStr.toLongLong(&sizeOk, 0);
    if (!sizeOk || expectedSize <= 0) {
        LOG_ERROR_CAT(LOG_TAG, QString("Invalid read size: '%1'").arg(sizeStr));
        return {};
    }

    return recvBinaryPayload(expectedSize);
}

bool XmlDaClient::erasePartition(const QString& name)
{
    LOG_INFO_CAT(LOG_TAG, QString("Erasing partition '%1' via XML DA").arg(name));

    QMap<QString, QString> params;
    params["partition"] = name;

    QString xml = buildXmlCommand(XmlDaCmd::CMD_ERASE_PARTITION, params);
    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

bool XmlDaClient::formatPartition(const QString& name)
{
    QMap<QString, QString> params;
    params["partition"] = name;

    QString xml = buildXmlCommand(XmlDaCmd::CMD_FORMAT_PARTITION, params);
    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

// ── DA2 upload via BOOT-TO ───────────────────────────────────────────────────

bool XmlDaClient::uploadDa2(const DaEntry& da2)
{
    LOG_INFO_CAT(LOG_TAG, QString("Uploading DA2 via BOOT-TO: %1 bytes to 0x%2")
                              .arg(da2.data.size()).arg(da2.loadAddr, 8, 16, QChar('0')));

    // Build BOOT-TO command
    QMap<QString, QString> params;
    params["at_address"] = QString("0x%1").arg(da2.loadAddr, 8, 16, QChar('0'));
    params["jmp_address"] = QString("0x%1").arg(da2.entryAddr, 8, 16, QChar('0'));

    QString xml = buildXmlCommand("CMD:BOOT-TO", params);
    if (!sendXml(xml))
        return false;

    QDomDocument resp = recvXmlResponse();
    if (!isResponseOk(resp)) {
        LOG_ERROR_CAT(LOG_TAG, "BOOT-TO rejected by DA");
        return false;
    }

    // DA requests data with "OK@<size>" — read and respond
    // Send DA2 binary data
    return sendBinaryPayload(da2.data);
}

// ── Device info ─────────────────────────────────────────────────────────────

XmlDaInfo XmlDaClient::getDaInfo()
{
    XmlDaInfo info;

    QString xml = buildXmlCommand(XmlDaCmd::CMD_GET_DA_INFO);
    if (!sendXml(xml))
        return info;

    QDomDocument resp = recvXmlResponse();
    if (!isResponseOk(resp))
        return info;

    info.daVersion = getResponseField(resp, "da_version");
    info.flashType = getResponseField(resp, "flash_type").toUInt(nullptr, 0);
    info.flashSize = getResponseField(resp, "flash_size").toULongLong(nullptr, 0);

    return info;
}

// ── Control ─────────────────────────────────────────────────────────────────

bool XmlDaClient::reboot()
{
    QString xml = buildXmlCommand(XmlDaCmd::CMD_REBOOT);
    if (!sendXml(xml))
        return false;
    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

bool XmlDaClient::shutdown()
{
    QString xml = buildXmlCommand(XmlDaCmd::CMD_SHUTDOWN);
    if (!sendXml(xml))
        return false;
    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

// ── XML helpers ─────────────────────────────────────────────────────────────

QString XmlDaClient::buildXmlCommand(const QString& command,
                                      const QMap<QString, QString>& params) const
{
    QDomDocument doc;
    QDomElement root = doc.createElement("da");
    doc.appendChild(root);

    QDomElement verElem = doc.createElement("version");
    verElem.appendChild(doc.createTextNode(XML_VERSION));
    root.appendChild(verElem);

    QDomElement cmdElem = doc.createElement("command");
    cmdElem.appendChild(doc.createTextNode(command));
    root.appendChild(cmdElem);

    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        QDomElement elem = doc.createElement(it.key());
        elem.appendChild(doc.createTextNode(it.value()));
        root.appendChild(elem);
    }

    return doc.toString(-1); // compact, no indentation
}

bool XmlDaClient::sendXml(const QString& xml)
{
    QByteArray data = xml.toUtf8();

    // XFlash-style framing: [magic(4)][dataType(4)][length(4)] + [data]
    // magic = 0xFEEEEEEF, dataType = 1 (ProtocolFlow)
    QByteArray pkt;
    pkt.reserve(12 + data.size());

    uint32_t magic = qToLittleEndian(static_cast<uint32_t>(0xFEEEEEEF));
    uint32_t dataType = qToLittleEndian(static_cast<uint32_t>(1)); // DT_PROTOCOL_FLOW
    uint32_t len = qToLittleEndian(static_cast<uint32_t>(data.size()));

    pkt.append(reinterpret_cast<const char*>(&magic), 4);
    pkt.append(reinterpret_cast<const char*>(&dataType), 4);
    pkt.append(reinterpret_cast<const char*>(&len), 4);
    pkt.append(data);

    return m_transport->write(pkt) == pkt.size();
}

QDomDocument XmlDaClient::recvXmlResponse()
{
    QDomDocument doc;

    // Read 12-byte XFlash-style header: [magic(4)][dataType(4)][length(4)]
    QByteArray header = m_transport->readExact(12, DEFAULT_TIMEOUT);
    if (header.size() < 12) {
        // Fallback: try old-style 4-byte length prefix
        if (header.size() >= 4) {
            uint32_t len = qFromLittleEndian<uint32_t>(
                reinterpret_cast<const uchar*>(header.constData()));
            if (len > 0 && len < 1024 * 1024) {
                QByteArray xmlData = m_transport->readExact(static_cast<int>(len), DEFAULT_TIMEOUT);
                auto parseResult = doc.setContent(xmlData);
                if (!parseResult) {
                    LOG_ERROR_CAT(LOG_TAG, QString("XML parse error: %1").arg(parseResult.errorMessage));
                }
                return doc;
            }
        }
        LOG_ERROR_CAT(LOG_TAG, "Failed to read XML response header");
        return doc;
    }

    uint32_t magic = qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar*>(header.constData()));
    uint32_t len = qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar*>(header.constData() + 8));

    if (magic != 0xFEEEEEEF) {
        // Not XFlash framing — try to interpret header[0..3] as a length prefix (legacy)
        len = qFromLittleEndian<uint32_t>(
            reinterpret_cast<const uchar*>(header.constData()));
        if (len > 0 && len < 1024 * 1024) {
            // Already consumed 12 bytes, but the real data starts at offset 4
            // The "length" consumed bytes 0-3; bytes 4-11 are the first 8 bytes of XML
            QByteArray remaining;
            if (len > 8) {
                remaining = m_transport->readExact(static_cast<int>(len - 8), DEFAULT_TIMEOUT);
            }
            QByteArray xmlData = header.mid(4) + remaining;
            auto parseResult = doc.setContent(xmlData);
            if (!parseResult) {
                LOG_ERROR_CAT(LOG_TAG, QString("XML parse error: %1").arg(parseResult.errorMessage));
            }
            return doc;
        }
        LOG_WARNING_CAT(LOG_TAG, QString("Bad XML response magic: 0x%1").arg(magic, 8, 16, QChar('0')));
        return doc;
    }

    if (len == 0 || len > 1024 * 1024) {
        LOG_ERROR_CAT(LOG_TAG, QString("Invalid XML response length: %1").arg(len));
        return doc;
    }

    QByteArray xmlData = m_transport->readExact(static_cast<int>(len), DEFAULT_TIMEOUT);

    auto parseResult = doc.setContent(xmlData);
    if (!parseResult) {
        LOG_ERROR_CAT(LOG_TAG, QString("XML parse error at line %1: %2")
                                   .arg(parseResult.errorLine)
                                   .arg(parseResult.errorMessage));
    }

    return doc;
}

bool XmlDaClient::isResponseOk(const QDomDocument& doc) const
{
    QDomElement root = doc.documentElement();
    if (root.isNull())
        return false;

    QDomElement resultElem = root.firstChildElement("result");
    return resultElem.text().compare("OK", Qt::CaseInsensitive) == 0;
}

QString XmlDaClient::getResponseField(const QDomDocument& doc, const QString& field) const
{
    QDomElement root = doc.documentElement();
    if (root.isNull())
        return {};

    QDomElement elem = root.firstChildElement(field);
    return elem.isNull() ? QString() : elem.text();
}

// ── Binary payload transfer ─────────────────────────────────────────────────

bool XmlDaClient::sendBinaryPayload(const QByteArray& data)
{
    constexpr int BLOCK_SIZE = 0x40000; // 256 KiB
    qint64 totalSent = 0;
    const qint64 totalSize = data.size();

    while (totalSent < totalSize) {
        int chunkLen = static_cast<int>(qMin<qint64>(BLOCK_SIZE, totalSize - totalSent));
        QByteArray chunk = data.mid(static_cast<int>(totalSent), chunkLen);

        if (m_transport->write(chunk) != chunkLen) {
            LOG_ERROR_CAT(LOG_TAG, "Binary payload send failed");
            return false;
        }

        totalSent += chunkLen;
        emit transferProgress(totalSent, totalSize);
    }

    // Wait for final status XML
    QDomDocument resp = recvXmlResponse();
    return isResponseOk(resp);
}

QByteArray XmlDaClient::recvBinaryPayload(qint64 expectedSize)
{
    if (expectedSize <= 0)
        return {};

    constexpr int BLOCK_SIZE = 0x40000;
    QByteArray result;
    result.reserve(static_cast<int>(expectedSize));

    qint64 remaining = expectedSize;
    while (remaining > 0) {
        int chunkLen = static_cast<int>(qMin<qint64>(BLOCK_SIZE, remaining));
        QByteArray chunk = m_transport->readExact(chunkLen, DEFAULT_TIMEOUT);

        if (chunk.isEmpty()) {
            LOG_ERROR_CAT(LOG_TAG, QString("Binary payload recv failed: got %1/%2 bytes")
                                       .arg(result.size()).arg(expectedSize));
            break;
        }

        result.append(chunk);
        remaining -= chunk.size();
        emit transferProgress(expectedSize - remaining, expectedSize);
    }

    if (result.size() != expectedSize) {
        LOG_WARNING_CAT(LOG_TAG, QString("Incomplete binary payload: %1/%2 bytes")
                                     .arg(result.size()).arg(expectedSize));
    }

    return result;
}

} // namespace sakura
