#include "sprd_diag_client.h"
#include "spreadtrum/protocol/hdlc_protocol.h"
#include "transport/i_transport.h"
#include "core/logger.h"

#include <QtEndian>

namespace sakura {

static constexpr char LOG_TAG[] = "SPRD-DIAG";

SprdDiagClient::SprdDiagClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

SprdDiagClient::~SprdDiagClient() = default;

// ── Connection ──────────────────────────────────────────────────────────────

bool SprdDiagClient::connect()
{
    LOG_INFO_CAT(LOG_TAG, "Connecting via Diag protocol...");
    if (!sendDiagCommand(SprdDiagCmd::CMD_CONNECT))
        return false;

    QByteArray resp = recvDiagResponse();
    return isDiagOk(resp);
}

// ── NV operations ───────────────────────────────────────────────────────────

SprdNvItem SprdDiagClient::readNvItem(uint16_t itemId)
{
    SprdNvItem item;
    item.id = itemId;

    QByteArray payload;
    uint16_t beId = qToBigEndian(itemId);
    payload.append(reinterpret_cast<const char*>(&beId), 2);

    if (!sendDiagCommand(SprdDiagCmd::CMD_READ_NV, payload))
        return item;

    QByteArray resp = recvDiagResponse();
    if (resp.size() > 3 && isDiagOk(resp)) {
        item.data = resp.mid(3); // Skip cmd + status + id
        item.valid = true;
    }

    return item;
}

bool SprdDiagClient::writeNvItem(uint16_t itemId, const QByteArray& data)
{
    QByteArray payload;
    uint16_t beId = qToBigEndian(itemId);
    payload.append(reinterpret_cast<const char*>(&beId), 2);
    payload.append(data);

    if (!sendDiagCommand(SprdDiagCmd::CMD_WRITE_NV, payload))
        return false;

    QByteArray resp = recvDiagResponse();
    return isDiagOk(resp);
}

bool SprdDiagClient::deleteNvItem(uint16_t itemId)
{
    QByteArray payload;
    uint16_t beId = qToBigEndian(itemId);
    payload.append(reinterpret_cast<const char*>(&beId), 2);

    // Delete uses write command with empty data
    if (!sendDiagCommand(SprdDiagCmd::CMD_WRITE_NV, payload))
        return false;

    QByteArray resp = recvDiagResponse();
    return isDiagOk(resp);
}

// ── IMEI ────────────────────────────────────────────────────────────────────

QByteArray SprdDiagClient::readImei(int simSlot)
{
    QByteArray payload;
    payload.append(static_cast<char>(simSlot & 0xFF));

    if (!sendDiagCommand(SprdDiagCmd::CMD_READ_IMEI, payload))
        return {};

    QByteArray resp = recvDiagResponse();
    if (isDiagOk(resp) && resp.size() > 2) {
        return resp.mid(2); // Skip cmd + status
    }

    return {};
}

bool SprdDiagClient::writeImei(int simSlot, const QByteArray& imei)
{
    QByteArray payload;
    payload.append(static_cast<char>(simSlot & 0xFF));
    payload.append(imei);

    if (!sendDiagCommand(SprdDiagCmd::CMD_WRITE_IMEI, payload))
        return false;

    QByteArray resp = recvDiagResponse();
    return isDiagOk(resp);
}

// ── Device info ─────────────────────────────────────────────────────────────

QString SprdDiagClient::readVersion()
{
    if (!sendDiagCommand(SprdDiagCmd::CMD_READ_VERSION))
        return {};

    QByteArray resp = recvDiagResponse();
    if (isDiagOk(resp) && resp.size() > 2) {
        return QString::fromUtf8(resp.mid(2));
    }
    return {};
}

QByteArray SprdDiagClient::readChipId()
{
    if (!sendDiagCommand(SprdDiagCmd::CMD_READ_CHIPID))
        return {};

    QByteArray resp = recvDiagResponse();
    if (isDiagOk(resp) && resp.size() > 2) {
        return resp.mid(2);
    }
    return {};
}

SprdPhaseCheck SprdDiagClient::readPhaseCheck()
{
    SprdPhaseCheck phase;

    if (!sendDiagCommand(SprdDiagCmd::CMD_READ_PHASE))
        return phase;

    QByteArray resp = recvDiagResponse();
    if (!isDiagOk(resp) || resp.size() < 10)
        return phase;

    // Parse phase check structure
    // Response format: [cmd(1)][status(1)][sn(24)][station(8)][flags(4)][passed(1)]
    // After skipping cmd + status (2 bytes), payload = sn(24)+station(8)+flags(4)+passed(1) = 37 bytes
    const QByteArray data = resp.mid(2); // Skip cmd + status
    if (data.size() >= 37) {
        phase.sn      = QString::fromLatin1(data.mid(0, 24)).trimmed();
        phase.station  = QString::fromLatin1(data.mid(24, 8)).trimmed();
        phase.flags    = qFromBigEndian<uint32_t>(
            reinterpret_cast<const uchar*>(data.constData() + 32));
        phase.passed   = data[36] != 0;
    }

    return phase;
}

// ── Control ─────────────────────────────────────────────────────────────────

bool SprdDiagClient::reset()
{
    if (!sendDiagCommand(SprdDiagCmd::CMD_RESET))
        return false;
    return true; // No response expected after reset
}

bool SprdDiagClient::powerOff()
{
    if (!sendDiagCommand(SprdDiagCmd::CMD_POWER_OFF))
        return false;
    return true;
}

bool SprdDiagClient::enterCalibrationMode()
{
    LOG_INFO_CAT(LOG_TAG, "Entering calibration mode...");

    if (!sendDiagCommand(SprdDiagCmd::CMD_SET_CALIBRATION))
        return false;

    QByteArray resp = recvDiagResponse();
    return isDiagOk(resp);
}

// ── Private helpers ─────────────────────────────────────────────────────────

bool SprdDiagClient::sendDiagCommand(uint8_t cmd, const QByteArray& payload)
{
    // Diag commands use HDLC framing with cmd as the type
    QByteArray pkt = SprdHdlcProtocol::encode(static_cast<uint16_t>(cmd), payload, true);
    return m_transport->write(pkt) == pkt.size();
}

QByteArray SprdDiagClient::recvDiagResponse(int timeoutMs)
{
    QByteArray raw = m_transport->read(SprdHdlcProtocol::MAX_FRAME_SIZE, timeoutMs);
    if (raw.isEmpty()) {
        LOG_WARNING_CAT(LOG_TAG, "Diag response timeout");
        return {};
    }
    return SprdHdlcProtocol::decode(raw, true);
}

bool SprdDiagClient::isDiagOk(const QByteArray& resp) const
{
    if (resp.size() < 2)
        return false;
    // Response format: [cmd(1)][status(1)][data...]
    return static_cast<uint8_t>(resp[1]) == SprdDiagCmd::RESP_OK;
}

} // namespace sakura
