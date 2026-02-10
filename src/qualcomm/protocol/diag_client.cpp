#include "diag_client.h"
#include "transport/i_transport.h"
#include "common/hdlc_codec.h"
#include "core/logger.h"

#include <QDataStream>
#include <QFile>
#include <QtEndian>
#include <cstring>

static const QString TAG = QStringLiteral("Diag");

namespace sakura {

DiagClient::DiagClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

// ─── Low-level communication ─────────────────────────────────────────

QByteArray DiagClient::sendCommand(const QByteArray& payload, int timeoutMs)
{
    // HDLC-encode the payload (adds framing + CRC16)
    QByteArray frame = HdlcCodec::encode(payload);

    qint64 written = m_transport->write(frame);
    if (written != frame.size()) {
        LOG_ERROR_CAT(TAG, "Failed to write Diag command");
        return {};
    }

    // Read response — accumulate until we get a complete HDLC frame
    QByteArray buffer;
    int elapsed = 0;
    const int pollMs = 50;

    while (elapsed < timeoutMs) {
        QByteArray chunk = m_transport->read(4096, pollMs);
        if (!chunk.isEmpty()) {
            buffer.append(chunk);

            // Try to extract HDLC frames
            auto frames = HdlcCodec::extractFrames(buffer);
            if (!frames.isEmpty()) {
                // Decode first complete frame
                QByteArray decoded = HdlcCodec::decode(frames.first());
                if (!decoded.isEmpty())
                    return decoded;
            }
        }
        elapsed += pollMs;
    }

    LOG_WARNING_CAT(TAG, "Diag response timeout");
    return {};
}

QByteArray DiagClient::sendRawDiag(DiagCommand cmd, const QByteArray& payload)
{
    QByteArray pkt;
    pkt.append(static_cast<char>(static_cast<uint8_t>(cmd)));
    pkt.append(payload);
    return sendCommand(pkt);
}

// ─── Connect ─────────────────────────────────────────────────────────

bool DiagClient::connect()
{
    LOG_INFO_CAT(TAG, "Connecting to Diag interface");

    // Send version request as connectivity test
    QByteArray resp = sendRawDiag(DiagCommand::VERNO);
    if (resp.isEmpty()) {
        LOG_ERROR_CAT(TAG, "No response from Diag interface");
        return false;
    }

    if (static_cast<uint8_t>(resp[0]) == static_cast<uint8_t>(DiagCommand::VERNO)) {
        m_connected = true;
        LOG_INFO_CAT(TAG, "Diag connection established");
        emit statusMessage("Diag connected");
        return true;
    }

    LOG_ERROR_CAT(TAG, "Unexpected Diag response");
    return false;
}

void DiagClient::disconnect()
{
    m_connected = false;
    m_spcUnlocked = false;
}

// ─── SPC / Security ──────────────────────────────────────────────────

bool DiagClient::sendSpc(const QString& code)
{
    LOG_INFO_CAT(TAG, "Sending SPC");

    DiagSpcRequest req{};
    req.cmd = static_cast<uint8_t>(DiagCommand::SPC);

    QByteArray spcBytes = code.toLatin1();
    if (spcBytes.size() > 6) spcBytes.truncate(6);
    while (spcBytes.size() < 6) spcBytes.append('0');
    std::memcpy(req.spc, spcBytes.constData(), 6);

    QByteArray payload(reinterpret_cast<const char*>(&req), sizeof(req));
    QByteArray resp = sendCommand(payload);

    if (resp.size() >= static_cast<int>(sizeof(DiagSpcResponse))) {
        auto* spcResp = reinterpret_cast<const DiagSpcResponse*>(resp.constData());
        // Validate command echo
        if (spcResp->cmd != static_cast<uint8_t>(DiagCommand::SPC)) {
            LOG_WARNING_CAT(TAG, QString("SPC: unexpected cmd echo 0x%1")
                                     .arg(spcResp->cmd, 2, 16, QChar('0')));
            return false;
        }
        if (spcResp->status == 1) {
            m_spcUnlocked = true;
            LOG_INFO_CAT(TAG, "SPC accepted");
            return true;
        }
    }

    LOG_WARNING_CAT(TAG, "SPC rejected");
    return false;
}

bool DiagClient::sendPassword(const QString& password)
{
    LOG_INFO_CAT(TAG, "Sending security password");

    QByteArray payload;
    payload.append(static_cast<char>(static_cast<uint8_t>(DiagCommand::PASSWORD)));

    // Password is sent as 8-byte binary (padded with zeros)
    QByteArray pwBytes = password.toLatin1();
    pwBytes.resize(8, '\0');
    payload.append(pwBytes);

    QByteArray resp = sendCommand(payload);
    if (resp.size() >= 2 && static_cast<uint8_t>(resp[1]) == 1) {
        m_spcUnlocked = true;
        LOG_INFO_CAT(TAG, "Password accepted");
        return true;
    }

    LOG_WARNING_CAT(TAG, "Password rejected");
    return false;
}

// ─── NV operations ───────────────────────────────────────────────────

QByteArray DiagClient::readNv(uint16_t item)
{
    DiagNvReadRequest req{};
    req.cmd = static_cast<uint8_t>(DiagCommand::NV_READ);
    req.item = item;
    std::memset(req.data, 0, sizeof(req.data));
    req.status = 0;

    QByteArray payload(reinterpret_cast<const char*>(&req),
                       sizeof(uint8_t) + sizeof(uint16_t) + NV_DATA_SIZE + sizeof(uint16_t));
    QByteArray resp = sendCommand(payload);

    if (resp.size() < 3) {
        LOG_ERROR_CAT(TAG, QString("NV read failed for item %1").arg(item));
        return {};
    }

    // Check command echo
    if (static_cast<uint8_t>(resp[0]) != static_cast<uint8_t>(DiagCommand::NV_READ)) {
        LOG_ERROR_CAT(TAG, "Invalid NV read response");
        return {};
    }

    // Extract NV item number and status from response (Diag uses little-endian)
    uint16_t respItem = qFromLittleEndian<uint16_t>(
        reinterpret_cast<const uchar*>(resp.constData() + 1));
    if (respItem != item) {
        LOG_WARNING_CAT(TAG, QString("NV item mismatch: requested %1, got %2").arg(item).arg(respItem));
    }

    // Status is at the end: after cmd(1) + item(2) + data(128)
    if (resp.size() >= 1 + 2 + NV_DATA_SIZE + 2) {
        uint16_t status = qFromLittleEndian<uint16_t>(
            reinterpret_cast<const uchar*>(resp.constData() + 1 + 2 + NV_DATA_SIZE));
        if (status != static_cast<uint16_t>(NvStatus::Done)) {
            LOG_ERROR_CAT(TAG, QString("NV read status: %1").arg(status));
            return {};
        }
    }

    // Return the 128-byte data portion
    return resp.mid(3, NV_DATA_SIZE);
}

bool DiagClient::writeNv(uint16_t item, const QByteArray& data)
{
    if (!m_spcUnlocked) {
        LOG_WARNING_CAT(TAG, "SPC not unlocked, NV write may fail");
    }

    DiagNvWriteRequest req{};
    req.cmd = static_cast<uint8_t>(DiagCommand::NV_WRITE);
    req.item = item;
    std::memset(req.data, 0, sizeof(req.data));
    int copyLen = qMin(data.size(), static_cast<int>(NV_DATA_SIZE));
    std::memcpy(req.data, data.constData(), copyLen);
    req.status = 0;

    QByteArray payload(reinterpret_cast<const char*>(&req),
                       sizeof(uint8_t) + sizeof(uint16_t) + NV_DATA_SIZE + sizeof(uint16_t));
    QByteArray resp = sendCommand(payload);

    if (resp.size() < 3) {
        LOG_ERROR_CAT(TAG, QString("NV write failed for item %1").arg(item));
        return false;
    }

    // Check command echo
    if (static_cast<uint8_t>(resp[0]) != static_cast<uint8_t>(DiagCommand::NV_WRITE)) {
        LOG_ERROR_CAT(TAG, "Invalid NV write response");
        return false;
    }

    // Check status (little-endian)
    if (resp.size() >= 1 + 2 + NV_DATA_SIZE + 2) {
        uint16_t status = qFromLittleEndian<uint16_t>(
            reinterpret_cast<const uchar*>(resp.constData() + 1 + 2 + NV_DATA_SIZE));
        if (status != static_cast<uint16_t>(NvStatus::Done)) {
            LOG_ERROR_CAT(TAG, QString("NV write status: %1").arg(status));
            return false;
        }
    }

    LOG_INFO_CAT(TAG, QString("NV item %1 written").arg(item));
    return true;
}

// ─── IMEI encoding / decoding ────────────────────────────────────────

QByteArray DiagClient::encodeImei(const QString& imei)
{
    // IMEI is stored as BCD-encoded in NV item 550
    // Format: byte[0]=0x08 (length), byte[1..8] = BCD pairs
    if (imei.length() < 14)
        return {};

    QByteArray result(9, 0);
    result[0] = 0x08; // length byte = 8

    // First digit + check digit packing (3GPP TS 23.003)
    QString padded = imei;
    if (padded.length() == 15)
        padded = padded.left(14); // strip Luhn check for storage

    // Pack IMEI digits as BCD nibbles
    // Byte 1: 0xA | (digit1 << 4)  — type = IMEI (0x0A)
    result[1] = static_cast<char>(0x0A | ((padded[0].digitValue() & 0x0F) << 4));

    for (int i = 1; i < 14; i += 2) {
        int byteIdx = (i / 2) + 2;
        uint8_t lo = padded[i].digitValue() & 0x0F;
        uint8_t hi = (i + 1 < padded.length()) ? (padded[i + 1].digitValue() & 0x0F) : 0x0F;
        result[byteIdx] = static_cast<char>(lo | (hi << 4));
    }

    return result;
}

QString DiagClient::decodeImei(const QByteArray& data)
{
    // Decode BCD-encoded IMEI from NV item
    if (data.size() < 9)
        return {};

    QString imei;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.constData());

    // First digit is in upper nibble of byte[1]
    imei += QString::number((p[1] >> 4) & 0x0F);

    // Remaining digits: pairs from byte[2..8]
    for (int i = 2; i <= 8; ++i) {
        uint8_t lo = p[i] & 0x0F;
        uint8_t hi = (p[i] >> 4) & 0x0F;
        if (lo < 10) imei += QString::number(lo);
        if (hi < 10) imei += QString::number(hi);
    }

    return imei;
}

// ─── IMEI read/write ─────────────────────────────────────────────────

ImeiInfo DiagClient::readImei()
{
    ImeiInfo info;

    // IMEI 1 is NV item 550
    QByteArray data1 = readNv(static_cast<uint16_t>(NvItem::NV_IMEI));
    if (!data1.isEmpty()) {
        info.imei1 = decodeImei(data1);
        info.valid = !info.imei1.isEmpty();
    }

    // IMEI 2 is NV item 550 + subscription index (typically 551)
    QByteArray data2 = readNv(static_cast<uint16_t>(NvItem::NV_IMEI) + 1);
    if (!data2.isEmpty()) {
        info.imei2 = decodeImei(data2);
    }

    if (info.valid) {
        LOG_INFO_CAT(TAG, QString("IMEI1: %1, IMEI2: %2").arg(info.imei1, info.imei2));
    }

    return info;
}

bool DiagClient::writeImei(const QString& imei1, const QString& imei2)
{
    if (!m_spcUnlocked) {
        LOG_WARNING_CAT(TAG, "SPC not unlocked — IMEI write may be rejected");
    }

    bool ok = true;
    if (!imei1.isEmpty()) {
        QByteArray encoded = encodeImei(imei1);
        if (encoded.isEmpty() || !writeNv(static_cast<uint16_t>(NvItem::NV_IMEI), encoded)) {
            LOG_ERROR_CAT(TAG, "Failed to write IMEI1");
            ok = false;
        }
    }

    if (!imei2.isEmpty()) {
        QByteArray encoded = encodeImei(imei2);
        if (encoded.isEmpty() || !writeNv(static_cast<uint16_t>(NvItem::NV_IMEI) + 1, encoded)) {
            LOG_ERROR_CAT(TAG, "Failed to write IMEI2");
            ok = false;
        }
    }

    return ok;
}

// ─── Device info ─────────────────────────────────────────────────────

DiagDeviceInfo DiagClient::readDeviceInfo()
{
    DiagDeviceInfo info;

    QByteArray resp = sendRawDiag(DiagCommand::VERNO);
    if (resp.size() >= static_cast<int>(sizeof(DiagVersionResponse))) {
        auto* ver = reinterpret_cast<const DiagVersionResponse*>(resp.constData());
        info.compDate = QString::fromLatin1(ver->compDate, 11).trimmed();
        info.compTime = QString::fromLatin1(ver->compTime, 8).trimmed();
        info.swVersion = QString::fromLatin1(ver->verDir, 8).trimmed();
        info.modelId = QString::number(ver->mobModel);
    }

    // Read ESN
    QByteArray esnData = readNv(static_cast<uint16_t>(NvItem::NV_ESN));
    if (esnData.size() >= 4) {
        uint32_t esn = 0;
        std::memcpy(&esn, esnData.constData(), 4);
        info.esn = QString("0x%1").arg(esn, 8, 16, QChar('0'));
    }

    // Read MEID
    QByteArray meidData = readNv(static_cast<uint16_t>(NvItem::NV_MEID));
    if (meidData.size() >= 7) {
        info.meid = QString(meidData.left(7).toHex()).toUpper();
    }

    return info;
}

// ─── QCN backup ──────────────────────────────────────────────────────

bool DiagClient::readQcn(const QString& savePath)
{
    LOG_INFO_CAT(TAG, QString("Reading QCN backup to %1").arg(savePath));

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open file: %1").arg(savePath));
        return false;
    }

    // QCN is a collection of NV items — read known ranges
    // Standard NV items: 0 ~ 6999
    static constexpr uint16_t NV_MAX = 7000;
    int successCount = 0;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    // Simple QCN format: [item_id(2) | data_len(2) | data(128)] per entry
    for (uint16_t item = 0; item < NV_MAX; ++item) {
        QByteArray data = readNv(item);
        if (!data.isEmpty()) {
            out << item;
            out << static_cast<uint16_t>(data.size());
            out.writeRawData(data.constData(), data.size());
            successCount++;
        }

        if (item % 500 == 0) {
            emit transferProgress(item, NV_MAX);
            LOG_DEBUG_CAT(TAG, QString("QCN progress: %1/%2 (%3 items read)")
                            .arg(item).arg(NV_MAX).arg(successCount));
        }
    }

    file.close();
    LOG_INFO_CAT(TAG, QString("QCN backup complete: %1 NV items").arg(successCount));
    return successCount > 0;
}

// ─── Mode switching ──────────────────────────────────────────────────

bool DiagClient::switchToDownloadMode()
{
    LOG_INFO_CAT(TAG, "Switching to download mode");
    QByteArray resp = sendRawDiag(DiagCommand::DLOAD);
    // Device should disconnect and reappear in EDL/Sahara mode
    return !resp.isEmpty();
}

bool DiagClient::reboot()
{
    LOG_INFO_CAT(TAG, "Sending reboot command");

    // Diag mode reset command
    QByteArray payload;
    payload.append(static_cast<char>(static_cast<uint8_t>(DiagCommand::REBOOT)));
    payload.append(static_cast<char>(0x00)); // mode: normal reboot
    payload.append(static_cast<char>(0x00));

    sendCommand(payload);
    // Device will disconnect after reboot — command delivery is the success criterion
    LOG_INFO_CAT(TAG, "Reboot command sent, device will disconnect");
    return true;
}

// ─── EFS read ────────────────────────────────────────────────────────

QByteArray DiagClient::efsRead(const QString& path)
{
    // EFS2 file operations go through the Diag subsystem dispatch:
    //   Command 0x4B (DIAG_SUBSYS_CMD_F) with subsystem 0x13 (FS)
    //   Sub-commands: OPEN=0x01, READ=0x03, CLOSE=0x04

    LOG_INFO_CAT(TAG, QString("EFS read: %1").arg(path));

    // === Step 1: Open file ===
    // Subsys dispatch header: [0x4B][subsys_id=0x13][subcmd_le16=0x0001]
    // + [oflag=O_RDONLY=0x00000000][mode=0x00000000][path_string_null_terminated]
    QByteArray openCmd;
    openCmd.append(static_cast<char>(0x4B));     // DIAG_SUBSYS_CMD_F
    openCmd.append(static_cast<char>(0x13));     // Subsystem: FS
    uint16_t openSubCmd = 0x0001;                // EFS2_DIAG_OPEN
    openCmd.append(reinterpret_cast<const char*>(&openSubCmd), 2);
    uint32_t oflag = 0x00000000;                 // O_RDONLY
    openCmd.append(reinterpret_cast<const char*>(&oflag), 4);
    uint32_t mode = 0x00000000;
    openCmd.append(reinterpret_cast<const char*>(&mode), 4);
    QByteArray pathBytes = path.toUtf8();
    openCmd.append(pathBytes);
    openCmd.append('\0'); // Null terminator

    QByteArray openResp = sendCommand(openCmd);
    if (openResp.size() < 12) {
        LOG_ERROR_CAT(TAG, QString("EFS open failed — response too short (%1 bytes)")
                            .arg(openResp.size()));
        return {};
    }

    // Response: [0x4B][0x13][subcmd(2)][fd(4)][errno(4)] — all little-endian
    int32_t fd = qFromLittleEndian<int32_t>(
        reinterpret_cast<const uchar*>(openResp.constData() + 4));
    int32_t efsErrno = qFromLittleEndian<int32_t>(
        reinterpret_cast<const uchar*>(openResp.constData() + 8));

    if (fd < 0 || efsErrno != 0) {
        LOG_ERROR_CAT(TAG, QString("EFS open failed: fd=%1 errno=%2").arg(fd).arg(efsErrno));
        return {};
    }

    LOG_DEBUG_CAT(TAG, QString("EFS file opened: fd=%1").arg(fd));

    // === Step 2: Read file in chunks ===
    QByteArray fileData;
    constexpr int READ_CHUNK = 512; // EFS read chunk size

    while (true) {
        QByteArray readCmd;
        readCmd.append(static_cast<char>(0x4B));
        readCmd.append(static_cast<char>(0x13));
        uint16_t readSubCmd = 0x0003; // EFS2_DIAG_READ
        readCmd.append(reinterpret_cast<const char*>(&readSubCmd), 2);
        readCmd.append(reinterpret_cast<const char*>(&fd), 4);
        uint32_t nbyte = READ_CHUNK;
        readCmd.append(reinterpret_cast<const char*>(&nbyte), 4);
        uint32_t offset = static_cast<uint32_t>(fileData.size());
        readCmd.append(reinterpret_cast<const char*>(&offset), 4);

        QByteArray readResp = sendCommand(readCmd);
        // Response: [0x4B][0x13][subcmd(2)][fd(4)][offset(4)][bytes_read(4)][errno(4)][data...]
        if (readResp.size() < 20) break;

        int32_t bytesRead = qFromLittleEndian<int32_t>(
            reinterpret_cast<const uchar*>(readResp.constData() + 12));
        int32_t readErr = qFromLittleEndian<int32_t>(
            reinterpret_cast<const uchar*>(readResp.constData() + 16));

        if (bytesRead <= 0 || readErr != 0) break;

        // Data starts at offset 20
        int available = readResp.size() - 20;
        int toRead = qMin(bytesRead, available);
        if (toRead > 0) {
            fileData.append(readResp.mid(20, toRead));
        }

        if (bytesRead < READ_CHUNK) break; // EOF
    }

    // === Step 3: Close file ===
    QByteArray closeCmd;
    closeCmd.append(static_cast<char>(0x4B));
    closeCmd.append(static_cast<char>(0x13));
    uint16_t closeSubCmd = 0x0004; // EFS2_DIAG_CLOSE
    closeCmd.append(reinterpret_cast<const char*>(&closeSubCmd), 2);
    closeCmd.append(reinterpret_cast<const char*>(&fd), 4);
    sendCommand(closeCmd);

    LOG_INFO_CAT(TAG, QString("EFS read complete: %1 bytes from %2")
                        .arg(fileData.size()).arg(path));
    return fileData;
}

} // namespace sakura
