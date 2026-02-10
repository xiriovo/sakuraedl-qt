#include "mediatek_service.h"
#include "mediatek/protocol/xflash_client.h"  // XFlashConst::MAGIC
#include "mediatek/protocol/xml_da_client.h"
#include "mediatek/auth/mtk_sla_auth.h"
#include "mediatek/database/mtk_chip_database.h"
#include "transport/i_transport.h"
#include "core/logger.h"

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-SVC";

MediatekService::MediatekService(QObject* parent)
    : QObject(parent)
{
}

MediatekService::~MediatekService()
{
    disconnect();
}

// ── Connection ──────────────────────────────────────────────────────────────

bool MediatekService::connectDevice(ITransport* transport)
{
    if (m_connected) {
        LOG_WARNING_CAT(LOG_TAG, "Already connected, disconnecting first");
        disconnect();
    }

    m_transport = transport;
    m_bromClient = std::make_unique<BromClient>(transport, this);

    // Step 1: BROM handshake
    if (!performHandshake()) {
        emit operationCompleted(false, "BROM handshake failed");
        return false;
    }

    // Step 2: Read device identity
    m_deviceInfo = m_bromClient->getDeviceInfo();
    m_deviceInfo.comPort = transport->description();

    LOG_INFO_CAT(LOG_TAG, QString("Connected: HW=0x%1 ME_ID=%2")
                              .arg(m_deviceInfo.hwCode, 4, 16, QChar('0'))
                              .arg(QString(m_deviceInfo.meId.toHex())));

    emit deviceInfoReady(m_deviceInfo);

    // Step 3: Handle secure boot if needed
    if (m_deviceInfo.targetCfg.slaEnabled) {
        if (!handleSecureBoot()) {
            LOG_WARNING_CAT(LOG_TAG, "SLA authentication failed — DA may be rejected");
        }
    }

    m_connected = true;
    emit stateChanged(2); // BromMode
    return true;
}

void MediatekService::disconnect()
{
    m_xflashClient.reset();
    m_xmlDaClient.reset();
    m_bromClient.reset();
    m_slaAuth.reset();
    m_transport = nullptr;
    m_connected = false;
    m_deviceInfo = {};

    emit stateChanged(0); // Disconnected
}

// ── DA management ───────────────────────────────────────────────────────────

bool MediatekService::loadDaFile(const QString& path)
{
    LOG_INFO_CAT(LOG_TAG, QString("Loading DA file: %1").arg(path));

    if (!m_daLoader.parseDaFile(path)) {
        emit operationCompleted(false, "Failed to parse DA file: " + m_daLoader.errorString());
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, QString("DA file loaded: %1 entries").arg(m_daLoader.entryCount()));
    return true;
}

bool MediatekService::downloadDa()
{
    if (!m_connected || !m_bromClient) {
        emit operationCompleted(false, "Not connected");
        return false;
    }

    if (!m_daLoader.isValid()) {
        emit operationCompleted(false, "No DA file loaded");
        return false;
    }

    return detectAndLoadDa();
}

// ── Partition operations ────────────────────────────────────────────────────

QList<PartitionInfo> MediatekService::readPartitions()
{
    QList<PartitionInfo> partitions;

    if (m_xflashClient) {
        partitions = m_xflashClient->readPartitions();
    } else if (m_xmlDaClient) {
        partitions = m_xmlDaClient->readPartitions();
    } else {
        emit operationCompleted(false, "No DA client active");
        return {};
    }

    emit partitionsReady(partitions);
    return partitions;
}

bool MediatekService::writePartition(const QString& name, const QByteArray& data)
{
    if (m_xflashClient)
        return m_xflashClient->writePartition(name, data);
    if (m_xmlDaClient)
        return m_xmlDaClient->writePartition(name, data);

    emit operationCompleted(false, "No DA client active");
    return false;
}

QByteArray MediatekService::readPartition(const QString& name, qint64 offset, qint64 length)
{
    if (m_xflashClient)
        return m_xflashClient->readPartition(name, offset, length);
    if (m_xmlDaClient)
        return m_xmlDaClient->readPartition(name, offset, length);

    emit operationCompleted(false, "No DA client active");
    return {};
}

bool MediatekService::erasePartition(const QString& name)
{
    if (m_xflashClient)
        return m_xflashClient->erasePartition(name);
    if (m_xmlDaClient)
        return m_xmlDaClient->erasePartition(name);

    emit operationCompleted(false, "No DA client active");
    return false;
}

bool MediatekService::formatAll()
{
    // Read current partition table, then erase each partition
    auto parts = readPartitions();
    if (parts.isEmpty()) {
        emit operationCompleted(false, "Cannot read partition table for format");
        return false;
    }

    int ok = 0, fail = 0;
    for (const auto& p : parts) {
        // Skip critical partitions
        if (p.name == "preloader" || p.name == "pgpt" || p.name == "sgpt")
            continue;
        LOG_INFO_CAT(LOG_TAG, QString("Formatting: %1").arg(p.name));
        if (erasePartition(p.name)) {
            ok++;
        } else {
            fail++;
            LOG_WARNING_CAT(LOG_TAG, QString("Failed to erase: %1").arg(p.name));
        }
    }

    QString msg = QString("Format complete: %1 OK, %2 failed").arg(ok).arg(fail);
    emit operationCompleted(fail == 0, msg);
    return fail == 0;
}

// ── Device info ─────────────────────────────────────────────────────────────

QString MediatekService::chipName() const
{
    return MtkChipDatabase::instance().chipName(m_deviceInfo.hwCode);
}

// ── Control ─────────────────────────────────────────────────────────────────

bool MediatekService::reboot()
{
    if (m_xflashClient)
        return m_xflashClient->reboot();
    if (m_xmlDaClient)
        return m_xmlDaClient->reboot();
    return false;
}

bool MediatekService::shutdown()
{
    if (m_xflashClient)
        return m_xflashClient->shutdown();
    if (m_xmlDaClient)
        return m_xmlDaClient->shutdown();
    return false;
}

// ── Internal flow ───────────────────────────────────────────────────────────

bool MediatekService::performHandshake()
{
    LOG_INFO_CAT(LOG_TAG, "Performing BROM handshake...");
    return m_bromClient->handshake();
}

bool MediatekService::detectAndLoadDa()
{
    // Find DA1 for this HW code
    DaEntry da1 = m_daLoader.findDa1ForHwCode(m_deviceInfo.hwCode);
    if (!da1.isValid()) {
        emit operationCompleted(false, "No matching DA1 for this device");
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, QString("Sending DA1 '%1' to 0x%2")
                              .arg(da1.name)
                              .arg(da1.loadAddr, 8, 16, QChar('0')));

    // Send DA1
    if (!m_bromClient->sendDa(da1.data, da1.loadAddr, da1.signatureLen)) {
        emit operationCompleted(false, "Failed to send DA1");
        return false;
    }

    // Jump to DA1
    if (!m_bromClient->jumpDa(da1.entryAddr)) {
        emit operationCompleted(false, "Failed to jump to DA1");
        return false;
    }

    emit stateChanged(4); // Da1Loaded

    // Negotiate protocol with the running DA
    if (!negotiateProtocol()) {
        emit operationCompleted(false, "DA protocol negotiation failed");
        return false;
    }

    // Find and send DA2 if needed via the negotiated protocol
    DaEntry da2 = m_daLoader.findDa2ForHwCode(m_deviceInfo.hwCode);
    if (da2.isValid()) {
        LOG_INFO_CAT(LOG_TAG, QString("Sending DA2 '%1' (%2 bytes, load=0x%3)")
                                  .arg(da2.name).arg(da2.data.size())
                                  .arg(da2.loadAddr, 8, 16, QChar('0')));
        if (m_xmlDaClient) {
            // XML DA V6: upload DA2 via BOOT-TO command
            if (!m_xmlDaClient->uploadDa2(da2)) {
                LOG_WARNING_CAT(LOG_TAG, "DA2 upload via XML failed — device may handle DA2 internally");
            }
        } else if (m_xflashClient) {
            // XFlash: send DA2 via BROM send_da/jump_da (DA1 re-exposes BROM commands)
            if (!m_bromClient->sendDa(da2.data, da2.loadAddr, da2.signatureLen)) {
                LOG_WARNING_CAT(LOG_TAG, "DA2 send failed");
            } else if (!m_bromClient->jumpDa(da2.entryAddr)) {
                LOG_WARNING_CAT(LOG_TAG, "DA2 jump failed");
            }
        }
        emit stateChanged(5); // Da2Loaded
    }

    emit stateChanged(6); // Ready
    return true;
}

bool MediatekService::negotiateProtocol()
{
    // After jumping to DA1, the device sends a sync byte 0xC0.
    // Host must respond with 0x69 to acknowledge.
    // Then the DA sends its info packet — we inspect it to determine protocol.

    if (!m_transport) return false;

    // Phase 1: Wait for DA sync byte (0xC0)
    bool synced = false;
    for (int retry = 0; retry < 50; ++retry) {
        QByteArray chunk = m_transport->read(1, 100);
        if (!chunk.isEmpty() && static_cast<uint8_t>(chunk[0]) == 0xC0) {
            synced = true;
            break;
        }
    }

    if (!synced) {
        LOG_ERROR_CAT(LOG_TAG, "DA sync failed: no 0xC0 received after jump");
        return false;
    }

    // Phase 2: Send sync ACK (0x69)
    QByteArray ack(1, static_cast<char>(0x69));
    m_transport->write(ack);
    LOG_INFO_CAT(LOG_TAG, "DA sync: received 0xC0, sent 0x69 ack");

    // Phase 3: Read DA info/init data to determine protocol
    // Modern DAs send an XFlash-style header [magic(4)][dataType(4)][length(4)]
    // Legacy DAs may send raw data or XML
    QByteArray initBuf;
    for (int retry = 0; retry < 50; ++retry) {
        QByteArray chunk = m_transport->read(256, 100);
        if (!chunk.isEmpty()) {
            initBuf.append(chunk);
            // Try to read more if available
            QByteArray more = m_transport->read(4096, 50);
            if (!more.isEmpty()) initBuf.append(more);
            break;
        }
    }

    // Detect protocol from init data
    bool isXFlashFrame = false;
    bool isXml = false;

    if (initBuf.size() >= 12) {
        // Check for XFlash magic header: 0xFEEEEEEF
        uint32_t magic = qFromLittleEndian<uint32_t>(
            reinterpret_cast<const uchar*>(initBuf.constData()));
        if (magic == XFlashConst::MAGIC) {
            isXFlashFrame = true;
            // The data inside the XFlash frame may be XML
            uint32_t dataType = qFromLittleEndian<uint32_t>(
                reinterpret_cast<const uchar*>(initBuf.constData() + 4));
            uint32_t length = qFromLittleEndian<uint32_t>(
                reinterpret_cast<const uchar*>(initBuf.constData() + 8));
            if (length > 0 && initBuf.size() > 12) {
                QByteArray payload = initBuf.mid(12);
                isXml = payload.contains("<?xml") || payload.contains("<da");
            }
            Q_UNUSED(dataType);
        }
    }

    // Also check raw content (some old DAs send unframed XML)
    if (!isXFlashFrame) {
        isXml = initBuf.contains("<?xml") || initBuf.contains("<da>");
    }

    if (m_protocol == MtkDaProtocol::Auto) {
        if (isXml) {
            m_protocol = MtkDaProtocol::XmlDa;
            LOG_INFO_CAT(LOG_TAG, "Auto-detected XML DA V6 protocol");
        } else {
            m_protocol = MtkDaProtocol::XFlash;
            LOG_INFO_CAT(LOG_TAG, "Auto-detected XFlash binary protocol");
        }
    }

    if (m_protocol == MtkDaProtocol::XFlash) {
        m_xflashClient = std::make_unique<XFlashClient>(m_transport, this);
        connect(m_xflashClient.get(), &XFlashClient::transferProgress,
                this, &MediatekService::transferProgress);
        return true;
    }

    if (m_protocol == MtkDaProtocol::XmlDa) {
        m_xmlDaClient = std::make_unique<XmlDaClient>(m_transport, this);
        connect(m_xmlDaClient.get(), &XmlDaClient::transferProgress,
                this, &MediatekService::transferProgress);
        return m_xmlDaClient->notifyInit();
    }

    return false;
}

bool MediatekService::handleSecureBoot()
{
    LOG_INFO_CAT(LOG_TAG, "Device has SLA enabled — attempting authentication");

    m_slaAuth = std::make_unique<MtkSlaAuth>();

    // Use the high-level authenticate() method which handles:
    //   1. Get SLA challenge from BROM via sendCert/sendAuth
    //   2. Sign challenge with DA RSA private key
    //   3. Send signed response back to BROM
    if (!m_slaAuth->authenticate(m_bromClient.get())) {
        LOG_WARNING_CAT(LOG_TAG, "SLA authentication failed — DA may be rejected by secure boot");
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, "SLA authentication successful");
    return true;
}

} // namespace sakura
