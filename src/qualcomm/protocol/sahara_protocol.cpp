#include "sahara_protocol.h"
#include "transport/i_transport.h"
#include "core/logger.h"
#include "qualcomm/database/qualcomm_chip_db.h"

#include <QThread>
#include <cstring>

static const QString TAG = QStringLiteral("Sahara");

namespace sakura {

SaharaClient::SaharaClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

// ─── Low-level I/O helpers ───────────────────────────────────────────

QByteArray SaharaClient::readPacket(int timeoutMs)
{
    // Read 8-byte header first
    QByteArray header = m_transport->readExact(sizeof(SaharaPacketHeader), timeoutMs);
    if (header.size() < static_cast<int>(sizeof(SaharaPacketHeader))) {
        LOG_ERROR_CAT(TAG, "Failed to read Sahara packet header");
        return {};
    }

    auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(header.constData());
    uint32_t totalLen = hdr->length;

    if (totalLen < sizeof(SaharaPacketHeader)) {
        LOG_WARNING_CAT(TAG, QString("Packet length %1 < header size").arg(totalLen));
        return header;
    }

    uint32_t remaining = totalLen - sizeof(SaharaPacketHeader);
    if (remaining == 0)
        return header;

    if (remaining > 65536) {
        LOG_ERROR_CAT(TAG, QString("Packet body too large: %1 bytes").arg(remaining));
        return {};
    }

    QByteArray body = m_transport->readExact(remaining, timeoutMs);
    if (body.size() < static_cast<int>(remaining)) {
        LOG_ERROR_CAT(TAG, QString("Short read: expected %1 bytes, got %2")
                        .arg(remaining).arg(body.size()));
        return {};
    }

    return header + body;
}

bool SaharaClient::sendPacket(const void* data, uint32_t size)
{
    QByteArray pkt(reinterpret_cast<const char*>(data), size);
    qint64 written = m_transport->write(pkt);
    return written == static_cast<qint64>(size);
}

// ─── Send helpers ────────────────────────────────────────────────────

void SaharaClient::sendHelloResponse(SaharaMode mode)
{
    SaharaHelloResponsePacket resp{};
    resp.header.command = static_cast<uint32_t>(SaharaCommand::HelloResponse);
    resp.header.length  = sizeof(SaharaHelloResponsePacket);
    resp.version        = SAHARA_VERSION;
    resp.versionMin     = SAHARA_VERSION_MIN;
    resp.status         = static_cast<uint32_t>(SaharaStatus::Success);
    resp.mode           = static_cast<uint32_t>(mode);

    LOG_INFO_CAT(TAG, QString("Sending HelloResponse, mode=%1")
                    .arg(static_cast<uint32_t>(mode)));
    sendPacket(&resp, sizeof(resp));
}

void SaharaClient::sendSwitchMode(SaharaMode mode)
{
    SaharaSwitchModePacket pkt{};
    pkt.header.command = static_cast<uint32_t>(SaharaCommand::SwitchMode);
    pkt.header.length  = sizeof(SaharaSwitchModePacket);
    pkt.mode           = static_cast<uint32_t>(mode);

    LOG_INFO_CAT(TAG, QString("Sending SwitchMode to mode=%1")
                    .arg(static_cast<uint32_t>(mode)));
    sendPacket(&pkt, sizeof(pkt));
}

// ─── Handshake ───────────────────────────────────────────────────────

bool SaharaClient::handshakeAsync(SaharaMode requestedMode)
{
    LOG_INFO_CAT(TAG, "Waiting for Sahara Hello...");

    static constexpr int MAX_HELLO_RETRIES = 5;
    QByteArray pkt;

    for (int attempt = 0; attempt < MAX_HELLO_RETRIES; ++attempt) {
        if (attempt > 0) {
            LOG_INFO_CAT(TAG, QString("Hello retry %1/%2...")
                                  .arg(attempt).arg(MAX_HELLO_RETRIES - 1));
            QByteArray stale = m_transport->read(4096, 100);
            if (!stale.isEmpty())
                LOG_INFO_CAT(TAG, QString("Flushed %1 stale bytes").arg(stale.size()));
            QThread::msleep(500);
        }

        int timeout = (attempt == 0) ? HELLO_TIMEOUT_MS : READ_TIMEOUT_MS;
        pkt = readPacket(timeout);

        if (pkt.size() >= static_cast<int>(sizeof(SaharaHelloPacket))) {
            auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(pkt.constData());
            if (hdr->command == static_cast<uint32_t>(SaharaCommand::Hello))
                break;
            LOG_WARNING_CAT(TAG, QString("Expected Hello (0x01), got 0x%1")
                                     .arg(hdr->command, 2, 16, QChar('0')));
        } else {
            LOG_WARNING_CAT(TAG, QString("Hello read: got %1 bytes (need %2)")
                                     .arg(pkt.size()).arg(sizeof(SaharaHelloPacket)));
        }
        pkt.clear();
    }

    if (pkt.isEmpty() || pkt.size() < static_cast<int>(sizeof(SaharaHelloPacket))) {
        LOG_ERROR_CAT(TAG, "Failed to receive Sahara Hello after all retries");
        return false;
    }

    auto* hello = reinterpret_cast<const SaharaHelloPacket*>(pkt.constData());

    m_deviceVersion = hello->version;
    m_deviceMinVersion = hello->versionMin;
    m_maxCmdLen = hello->maxCmdLen;
    m_currentMode = static_cast<SaharaMode>(hello->mode);

    LOG_INFO_CAT(TAG, QString("Device Sahara v%1 (min %2), mode=%3, maxCmd=%4")
                    .arg(m_deviceVersion).arg(m_deviceMinVersion)
                    .arg(hello->mode).arg(m_maxCmdLen));

    m_deviceInfo.saharaVersion = m_deviceVersion;
    m_deviceInfo.saharaMinVersion = m_deviceMinVersion;

    // ── Per edl2: Try reading chip info BEFORE entering Image Transfer ──
    // Only on first Hello, and only if device in ImageTransferPending mode
    if (!m_chipInfoAttempted &&
        m_currentMode == SaharaMode::ImageTransferPending) {
        m_chipInfoAttempted = true;

        bool gotChipInfo = tryReadChipInfo();
        if (gotChipInfo) {
            // Device sends new Hello after SwitchMode back
            pkt = readPacket(HELLO_TIMEOUT_MS);
            if (pkt.size() >= static_cast<int>(sizeof(SaharaHelloPacket))) {
                auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(pkt.constData());
                if (hdr->command != static_cast<uint32_t>(SaharaCommand::Hello)) {
                    LOG_WARNING_CAT(TAG, QString("Expected new Hello after SwitchMode, got 0x%1")
                                             .arg(hdr->command, 2, 16, QChar('0')));
                }
                auto* hello2 = reinterpret_cast<const SaharaHelloPacket*>(pkt.constData());
                m_currentMode = static_cast<SaharaMode>(hello2->mode);
                LOG_INFO_CAT(TAG, QString("New Hello received, mode=%1").arg(hello2->mode));
            } else {
                LOG_WARNING_CAT(TAG, "No new Hello after SwitchMode");
            }
        }
    }

    // Send HelloResponse with requested mode
    sendHelloResponse(requestedMode);
    m_currentMode = requestedMode;

    emit statusMessage("Sahara handshake successful");
    return true;
}

// ─── Try reading chip info via Command mode ──────────────────────────
// Per edl2: Send HelloResponse requesting Command, wait for CommandReady,
// read info, switch back. Returns true if Command mode was entered.

bool SaharaClient::tryReadChipInfo()
{
    if (m_skipCommandMode) {
        LOG_INFO_CAT(TAG, "Skipping Command mode (previously failed)");
        return false;
    }

    LOG_INFO_CAT(TAG, QString("Attempting Command mode for chip info (v%1)...")
                    .arg(m_deviceVersion));

    // Send HelloResponse requesting Command mode (0x03)
    sendHelloResponse(SaharaMode::Command);

    // Wait for CommandReady (0x0B)
    QByteArray resp = readPacket(CMD_TIMEOUT_MS);
    if (resp.size() < static_cast<int>(sizeof(SaharaPacketHeader))) {
        LOG_WARNING_CAT(TAG, "No response to Command mode request");
        m_skipCommandMode = true;
        return false;
    }

    auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(resp.constData());
    SaharaCommand cmdId = static_cast<SaharaCommand>(hdr->command);

    if (cmdId == SaharaCommand::CommandReady) {
        LOG_INFO_CAT(TAG, "Device accepted Command mode — reading chip info");

        // ── Common: Serial Number (cmd=0x01) ──
        QByteArray serialData = executeCommandSafe(SaharaExecCommand::SerialNumRead);
        parseSerial(serialData);

        // ── Common: PK Hash (cmd=0x03) ──
        QByteArray pkData = executeCommandSafe(SaharaExecCommand::OemPkHashRead);
        if (!pkData.isEmpty()) {
            int hashLen = qMin(pkData.size(), 48);
            m_deviceInfo.pkHash = pkData.left(hashLen);
            m_deviceInfo.pkHashHex = QString(pkData.left(hashLen).toHex());
            LOG_INFO_CAT(TAG, QString("- OEM PKHASH : %1").arg(m_deviceInfo.pkHashHex));
        }

        // ── V1/V2 vs V3: SEPARATE paths (per edl2) ──
        if (m_deviceVersion < 3) {
            readChipInfoV1V2();
        } else {
            readChipInfoV3();
        }

        m_deviceInfo.chipInfoRead = true;

        // Lookup chip name
        if (m_deviceInfo.msmId != 0) {
            m_deviceInfo.chipName = QualcommChipDb::chipNameForMsm(m_deviceInfo.msmId);
        }

        // Summary log
        LOG_INFO_CAT(TAG, "─── Sahara Chip Info Summary ───");
        LOG_INFO_CAT(TAG, QString("  Sahara Version : %1").arg(m_deviceVersion));
        LOG_INFO_CAT(TAG, QString("  Serial   : %1").arg(m_deviceInfo.serialHex));
        LOG_INFO_CAT(TAG, QString("  MSM HWID : 0x%1 | model_id:0x%2 | oem_id:0x%3")
                        .arg(m_deviceInfo.msmId, 8, 16, QChar('0'))
                        .arg(m_deviceInfo.modelId, 4, 16, QChar('0'))
                        .arg(m_deviceInfo.oemId, 4, 16, QChar('0')));
        if (!m_deviceInfo.chipName.isEmpty())
            LOG_INFO_CAT(TAG, QString("  CHIP     : %1").arg(m_deviceInfo.chipName));
        if (!m_deviceInfo.hwIdHex.isEmpty())
            LOG_INFO_CAT(TAG, QString("  HW_ID    : %1").arg(m_deviceInfo.hwIdHex));
        if (m_deviceInfo.sblVersion != 0)
            LOG_INFO_CAT(TAG, QString("  SBL Ver  : 0x%1").arg(m_deviceInfo.sblVersion, 8, 16, QChar('0')));

        // Switch back to Image Transfer mode
        sendSwitchMode(SaharaMode::ImageTransferPending);
        return true;

    } else if (cmdId == SaharaCommand::ReadData ||
               cmdId == SaharaCommand::ReadData64) {
        LOG_INFO_CAT(TAG, QString("Device rejected Command mode (v%1) — got ReadData")
                            .arg(m_deviceVersion));
        m_skipCommandMode = true;
        return false;

    } else if (cmdId == SaharaCommand::EndImageTransfer) {
        LOG_WARNING_CAT(TAG, "Device abnormal state (EndImageTransfer during Command)");
        m_skipCommandMode = true;
        return false;

    } else {
        LOG_WARNING_CAT(TAG, QString("Unknown response to Command mode: 0x%1")
                                 .arg(hdr->command, 2, 16, QChar('0')));
        m_skipCommandMode = true;
        return false;
    }
}

// ─── V1/V2 chip info reading ─────────────────────────────────────────
// Per edl2: MsmHwIdRead (0x02) + SblSwVersion (0x07)

void SaharaClient::readChipInfoV1V2()
{
    LOG_INFO_CAT(TAG, "Reading V1/V2 chip info...");

    // V1/V2: HWID via cmd=0x02
    QByteArray hwIdData = executeCommandSafe(SaharaExecCommand::MsmHwIdRead);
    if (hwIdData.size() >= 8) {
        parseHwIdV1V2(hwIdData);
    }

    // V1/V2: SBL SW Version via cmd=0x07
    QByteArray sblData = executeCommandSafe(SaharaExecCommand::SblSwVersion);
    if (sblData.size() >= 4) {
        uint32_t sblVer = 0;
        std::memcpy(&sblVer, sblData.constData(), 4);
        m_deviceInfo.sblVersion = sblVer;
        LOG_INFO_CAT(TAG, QString("- SBL SW Version : 0x%1").arg(sblVer, 8, 16, QChar('0')));
    }
}

// ─── V3 chip info reading ────────────────────────────────────────────
// Per edl2: ChipIdV3Read (0x0A) + SblInfoRead (0x06)
// V3 does NOT use MsmHwIdRead (0x02) or SblSwVersion (0x07)

void SaharaClient::readChipInfoV3()
{
    LOG_INFO_CAT(TAG, "Reading V3 chip info...");

    // V3: Extended chip info via cmd=0x0A
    QByteArray extInfo = executeCommandSafe(SaharaExecCommand::ChipIdV3Read);
    if (extInfo.size() >= 44) {
        parseV3ExtendedInfo(extInfo);
    } else {
        // cmd=0x0A not supported — try to infer from PK Hash
        LOG_WARNING_CAT(TAG, "V3 ChipIdV3Read failed or unsupported");
        if (!m_deviceInfo.pkHashHex.isEmpty()) {
            LOG_INFO_CAT(TAG, "Attempting vendor inference from PK Hash");
            // Vendor inference would go here if we had a PK Hash database
        }
    }

    // V3: SBL Info via cmd=0x06
    QByteArray sblInfo = executeCommandSafe(SaharaExecCommand::SblInfoRead);
    if (sblInfo.size() >= 4) {
        parseSblInfo(sblInfo);
    }

    // NOTE: PBL version (cmd=0x08) removed — some devices don't support it
    // and it causes handshake failure (per edl2 comment)
}

// ─── Execute command (matching edl2 flow exactly) ────────────────────
// 1. Host → Device: Execute (0x0D) with clientCommand
// 2. Device → Host: ExecuteData (0x0E) with clientCommand + dataLength
// 3. Host → Device: ExecuteResponse (0x0F) with clientCommand (confirm)
// 4. Device → Host: raw data bytes

QByteArray SaharaClient::executeCommandSafe(SaharaExecCommand cmd)
{
    // Per edl2: SblInfoRead uses longer timeout
    int timeout = (cmd == SaharaExecCommand::SblInfoRead) ? 5000 : 2000;

    // Step 1: Send Execute (0x0D)
    SaharaExecutePacket execPkt{};
    execPkt.header.command = static_cast<uint32_t>(SaharaCommand::Execute);
    execPkt.header.length  = sizeof(SaharaExecutePacket);
    execPkt.clientCommand  = static_cast<uint32_t>(cmd);

    if (!sendPacket(&execPkt, sizeof(execPkt))) {
        LOG_WARNING_CAT(TAG, QString("Failed to send Execute cmd=0x%1")
                        .arg(static_cast<uint32_t>(cmd), 2, 16, QChar('0')));
        return {};
    }

    // Step 2: Read response header (8 bytes)
    QByteArray headerData = m_transport->readExact(sizeof(SaharaPacketHeader), timeout);
    if (headerData.size() < static_cast<int>(sizeof(SaharaPacketHeader))) {
        return {};
    }

    auto* respHdr = reinterpret_cast<const SaharaPacketHeader*>(headerData.constData());
    uint32_t respCmd = respHdr->command;
    uint32_t respLen = respHdr->length;

    // Must be ExecuteData (0x0E)
    if (respCmd != static_cast<uint32_t>(SaharaCommand::ExecuteData)) {
        LOG_WARNING_CAT(TAG, QString("Expected ExecuteData (0x0E), got 0x%1")
                        .arg(respCmd, 2, 16, QChar('0')));
        // Drain remaining bytes if any
        if (respLen > 8) {
            m_transport->readExact(respLen - 8, 1000);
        }
        return {};
    }

    // Read rest of ExecuteData packet body (clientCommand + dataLength = 8 bytes)
    if (respLen <= 8) return {};

    QByteArray body = m_transport->readExact(respLen - 8, timeout);
    if (body.size() < 8) return {};

    uint32_t dataCmd = 0;
    uint32_t dataLen = 0;
    std::memcpy(&dataCmd, body.constData(), 4);
    std::memcpy(&dataLen, body.constData() + 4, 4);

    // Verify command echo and data length
    if (dataCmd != static_cast<uint32_t>(cmd) || dataLen == 0) {
        return {};
    }

    LOG_DEBUG_CAT(TAG, QString("ExecuteData: cmd=0x%1, dataLen=%2")
                    .arg(dataCmd, 2, 16, QChar('0')).arg(dataLen));

    // Step 3: Send ExecuteResponse (0x0F) to confirm and request data
    SaharaExecuteResponsePacket respPkt{};
    respPkt.header.command = static_cast<uint32_t>(SaharaCommand::ExecuteResponse);
    respPkt.header.length  = sizeof(SaharaExecuteResponsePacket);
    respPkt.clientCommand  = static_cast<uint32_t>(cmd);

    if (!sendPacket(&respPkt, sizeof(respPkt))) {
        LOG_WARNING_CAT(TAG, "Failed to send ExecuteResponse");
        return {};
    }

    // Step 4: Read actual data
    int dataTimeout = (dataLen > 1000) ? 10000 : timeout;
    QByteArray result = m_transport->readExact(dataLen, dataTimeout);
    if (result.size() != static_cast<int>(dataLen)) {
        LOG_WARNING_CAT(TAG, QString("Execute cmd=0x%1: expected %2 bytes, got %3")
                                 .arg(static_cast<uint32_t>(cmd), 2, 16, QChar('0'))
                                 .arg(dataLen).arg(result.size()));
    }
    return result;
}

// ─── Parse serial number ─────────────────────────────────────────────

void SaharaClient::parseSerial(const QByteArray& data)
{
    if (data.size() >= 4) {
        uint32_t serial = 0;
        std::memcpy(&serial, data.constData(), 4);
        m_deviceInfo.serial = serial;
        m_deviceInfo.serialHex = QString("0x%1").arg(serial, 8, 16, QChar('0'));
        m_deviceInfo.serialRaw = data;
        LOG_INFO_CAT(TAG, QString("- Chip Serial Number : %1")
                        .arg(QString::number(serial, 16).rightJustified(8, '0')));
    }
}

// ─── Parse V1/V2 HWID (cmd=0x02 response) ───────────────────────────
// Per edl2 ProcessHwIdData():
//   8 bytes little-endian uint64:
//     Bits  0-31: MSM_ID (full 32-bit chip ID)
//     Bits 32-47: OEM_ID (vendor ID)
//     Bits 48-63: MODEL_ID

void SaharaClient::parseHwIdV1V2(const QByteArray& data)
{
    if (data.size() < 8) return;

    m_deviceInfo.hwIdRaw = data;

    uint64_t hwid = 0;
    std::memcpy(&hwid, data.constData(), 8);

    uint32_t msmId   = static_cast<uint32_t>(hwid & 0xFFFFFFFF);
    uint16_t oemId   = static_cast<uint16_t>((hwid >> 32) & 0xFFFF);
    uint16_t modelId = static_cast<uint16_t>((hwid >> 48) & 0xFFFF);

    m_deviceInfo.msmId   = msmId;
    m_deviceInfo.oemId   = oemId;
    m_deviceInfo.modelId = modelId;

    // Per edl2: ChipHwId = hwid.ToString("x16")
    m_deviceInfo.hwIdHex = QString("0x") +
        QString::number(hwid, 16).rightJustified(16, '0').toUpper();

    LOG_INFO_CAT(TAG, QString("- MSM HWID : 0x%1 | model_id:0x%2 | oem_id:%3")
                    .arg(msmId, 0, 16)
                    .arg(modelId, 4, 16, QChar('0'))
                    .arg(oemId, 4, 16, QChar('0')));
    LOG_INFO_CAT(TAG, QString("- HW_ID : %1").arg(m_deviceInfo.hwIdHex));
}

// ─── Parse V3 extended info (cmd=0x0A response) ─────────────────────
// Per edl2 ProcessV3ExtendedInfo():
//   V3 returns 84 bytes:
//     Offset 0:  Chip Identifier V3 (4 bytes)
//     Offset 36: MSM_ID (4 bytes)
//     Offset 40: OEM_ID (2 bytes)
//     Offset 42: MODEL_ID (2 bytes)
//     Offset 44: Alternate OEM_ID (if offset 40 is 0)

void SaharaClient::parseV3ExtendedInfo(const QByteArray& data)
{
    if (data.size() < 44) return;

    // Chip Identifier V3
    uint32_t chipIdV3 = 0;
    std::memcpy(&chipIdV3, data.constData(), 4);
    if (chipIdV3 != 0) {
        LOG_INFO_CAT(TAG, QString("- Chip Identifier V3 : %1")
                        .arg(chipIdV3, 8, 16, QChar('0')));
    }

    // V3 standard: MSM @ offset 36
    uint32_t rawMsm = 0;
    std::memcpy(&rawMsm, data.constData() + 36, 4);

    uint16_t rawOem = 0;
    std::memcpy(&rawOem, data.constData() + 40, 2);

    uint16_t rawModel = 0;
    std::memcpy(&rawModel, data.constData() + 42, 2);

    // Check alternate OEM_ID at offset 44 (per edl2)
    if (rawOem == 0 && data.size() >= 46) {
        uint16_t altOemId = 0;
        std::memcpy(&altOemId, data.constData() + 44, 2);
        if (altOemId > 0 && altOemId < 0x1000) {
            rawOem = altOemId;
        }
    }

    if (rawMsm != 0 || rawOem != 0) {
        m_deviceInfo.msmId   = rawMsm;
        m_deviceInfo.oemId   = rawOem;
        m_deviceInfo.modelId = rawModel;

        // Per edl2: format as "00{msm:x6}{oem:x4}{model:x4}"
        m_deviceInfo.hwIdHex = QString("0x") +
            QString("00%1%2%3")
                .arg(rawMsm, 6, 16, QChar('0'))
                .arg(rawOem, 4, 16, QChar('0'))
                .arg(rawModel, 4, 16, QChar('0'))
                .toUpper();

        LOG_INFO_CAT(TAG, QString("- MSM HWID : 0x%1 | model_id:0x%2 | oem_id:%3")
                        .arg(rawMsm, 0, 16)
                        .arg(rawModel, 4, 16, QChar('0'))
                        .arg(rawOem, 4, 16, QChar('0')));
        LOG_INFO_CAT(TAG, QString("- HW_ID : %1").arg(m_deviceInfo.hwIdHex));
    }
}

// ─── Parse SBL Info (V3 only, cmd=0x06 response) ────────────────────
// Per edl2 ProcessSblInfo():
//   Offset 0: Serial Number (4 bytes)
//   Offset 4: SBL Version (4 bytes)
//   Offset 8-15: OEM Data (8 bytes, optional)

void SaharaClient::parseSblInfo(const QByteArray& data)
{
    m_deviceInfo.sblInfoRaw = data;

    if (data.size() >= 4) {
        uint32_t sblSerial = 0;
        std::memcpy(&sblSerial, data.constData(), 4);
        LOG_INFO_CAT(TAG, QString("- SBL Serial : 0x%1").arg(sblSerial, 8, 16, QChar('0')));
    }

    if (data.size() >= 8) {
        uint32_t sblVer = 0;
        std::memcpy(&sblVer, data.constData() + 4, 4);
        if (sblVer != 0 && sblVer != 0xFFFFFFFF) {
            m_deviceInfo.sblVersion = sblVer;
            LOG_INFO_CAT(TAG, QString("- SBL Version : 0x%1").arg(sblVer, 8, 16, QChar('0')));
        }
    }

    if (data.size() >= 16) {
        uint32_t oem1 = 0, oem2 = 0;
        std::memcpy(&oem1, data.constData() + 8, 4);
        std::memcpy(&oem2, data.constData() + 12, 4);
        if (oem1 != 0 || oem2 != 0) {
            LOG_INFO_CAT(TAG, QString("- SBL OEM Data : 0x%1 0x%2")
                            .arg(oem1, 8, 16, QChar('0'))
                            .arg(oem2, 8, 16, QChar('0')));
        }
    }
}

// ─── Read chip info (public API) ─────────────────────────────────────

QByteArray SaharaClient::readChipInfo(SaharaExecCommand cmd)
{
    return executeCommandSafe(cmd);
}

// ─── Upload loader ───────────────────────────────────────────────────

bool SaharaClient::uploadLoader(const QByteArray& loaderData)
{
    LOG_INFO_CAT(TAG, QString("Uploading loader (%1 bytes)").arg(loaderData.size()));

    qint64 totalSize = loaderData.size();
    qint64 sent = 0;

    while (true) {
        QByteArray pkt = readPacket(UPLOAD_TIMEOUT_MS);
        if (pkt.size() < static_cast<int>(sizeof(SaharaPacketHeader))) {
            LOG_ERROR_CAT(TAG, "Failed to read request during upload");
            return false;
        }

        auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(pkt.constData());
        SaharaCommand cmd = static_cast<SaharaCommand>(hdr->command);

        if (cmd == SaharaCommand::ReadData &&
            pkt.size() >= static_cast<int>(sizeof(SaharaReadDataPacket))) {
            auto* req = reinterpret_cast<const SaharaReadDataPacket*>(pkt.constData());
            uint32_t offset = req->offset;
            uint32_t length = req->length;

            if (offset > static_cast<uint32_t>(loaderData.size()) ||
                length > static_cast<uint32_t>(loaderData.size()) - offset) {
                LOG_ERROR_CAT(TAG, QString("ReadData out of range: off=%1 len=%2 total=%3")
                                .arg(offset).arg(length).arg(loaderData.size()));
                return false;
            }

            QByteArray chunk = loaderData.mid(offset, length);
            if (m_transport->write(chunk) != static_cast<qint64>(length)) {
                LOG_ERROR_CAT(TAG, "Failed to write loader chunk");
                return false;
            }
            sent += length;
            emit uploadProgress(sent, totalSize);

        } else if (cmd == SaharaCommand::ReadData64 &&
                   pkt.size() >= static_cast<int>(sizeof(SaharaReadData64Packet))) {
            auto* req = reinterpret_cast<const SaharaReadData64Packet*>(pkt.constData());
            uint64_t offset = req->offset;
            uint64_t length = req->length;

            if (offset > static_cast<uint64_t>(loaderData.size()) ||
                length > static_cast<uint64_t>(loaderData.size()) - offset) {
                LOG_ERROR_CAT(TAG, QString("ReadData64 out of range: off=%1 len=%2")
                                .arg(offset).arg(length));
                return false;
            }

            QByteArray chunk = loaderData.mid(static_cast<int>(offset), static_cast<int>(length));
            if (m_transport->write(chunk) != static_cast<qint64>(length)) {
                LOG_ERROR_CAT(TAG, "Failed to write loader chunk (64-bit)");
                return false;
            }
            sent += length;
            emit uploadProgress(sent, totalSize);

        } else if (cmd == SaharaCommand::EndImageTransfer) {
            auto* endPkt = reinterpret_cast<const SaharaEndImageTransferPacket*>(pkt.constData());
            if (endPkt->status != static_cast<uint32_t>(SaharaStatus::Success)) {
                LOG_ERROR_CAT(TAG, QString("Image transfer failed with status 0x%1")
                                .arg(endPkt->status, 2, 16, QChar('0')));
                return false;
            }

            LOG_INFO_CAT(TAG, "Image transfer complete, sending Done");

            SaharaDonePacket donePkt{};
            donePkt.header.command = static_cast<uint32_t>(SaharaCommand::Done);
            donePkt.header.length  = sizeof(SaharaDonePacket);
            if (!sendPacket(&donePkt, sizeof(donePkt))) {
                LOG_ERROR_CAT(TAG, "Failed to send Done");
                return false;
            }

            QByteArray doneResp = readPacket(READ_TIMEOUT_MS);
            if (doneResp.size() >= static_cast<int>(sizeof(SaharaDoneResponsePacket))) {
                auto* dr = reinterpret_cast<const SaharaDoneResponsePacket*>(doneResp.constData());
                if (dr->header.command != static_cast<uint32_t>(SaharaCommand::DoneResponse)) {
                    LOG_WARNING_CAT(TAG, QString("Expected DoneResponse, got 0x%1")
                                             .arg(dr->header.command, 2, 16, QChar('0')));
                }
                LOG_INFO_CAT(TAG, QString("Done response: imageTxStatus=%1").arg(dr->imageTxStatus));
            } else {
                LOG_WARNING_CAT(TAG, "No valid DoneResponse received");
            }

            emit statusMessage("Loader uploaded successfully");
            return true;

        } else {
            LOG_WARNING_CAT(TAG, QString("Unexpected command during upload: 0x%1")
                              .arg(hdr->command, 2, 16, QChar('0')));
            return false;
        }
    }
}

// ─── Send hard reset (0x07) ──────────────────────────────────────────

bool SaharaClient::sendReset()
{
    LOG_INFO_CAT(TAG, "Sending Sahara Hard Reset (0x07)");

    SaharaResetPacket pkt{};
    pkt.header.command = static_cast<uint32_t>(SaharaCommand::Reset);
    pkt.header.length  = sizeof(SaharaResetPacket);

    if (!sendPacket(&pkt, sizeof(pkt))) {
        LOG_ERROR_CAT(TAG, "Failed to send Reset");
        return false;
    }

    QByteArray resp = readPacket(2000);
    if (!resp.isEmpty()) {
        auto* hdr = reinterpret_cast<const SaharaPacketHeader*>(resp.constData());
        if (hdr->command == static_cast<uint32_t>(SaharaCommand::ResetResponse)) {
            LOG_INFO_CAT(TAG, "Hard Reset acknowledged by device");
        }
    }

    return true;
}

// ─── Send soft reset / state machine reset (0x13) ───────────────────

bool SaharaClient::sendResetStateMachine()
{
    LOG_INFO_CAT(TAG, "Sending Sahara ResetStateMachine (0x13)");

    SaharaPacketHeader pkt{};
    pkt.command = static_cast<uint32_t>(SaharaCommand::ResetStateMachine);
    pkt.length  = sizeof(SaharaPacketHeader);

    if (!sendPacket(&pkt, sizeof(pkt))) {
        LOG_ERROR_CAT(TAG, "Failed to send ResetStateMachine");
        return false;
    }

    return true;
}

} // namespace sakura
