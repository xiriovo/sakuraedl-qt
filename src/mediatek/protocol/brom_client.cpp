#include "brom_client.h"
#include "transport/i_transport.h"
#include "core/logger.h"
#include "common/crc_utils.h"

#include <QThread>
#include <QtEndian>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-BROM";

BromClient::BromClient(ITransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

BromClient::~BromClient() = default;

// ── Handshake (4-byte sync: A0 0A 50 05 → 5F F5 AF FA) ─────────────────────
// Based on mtkclient (bkerler) Port.py run_handshake()

bool BromClient::handshake()
{
    LOG_INFO_CAT(LOG_TAG, "Starting BROM handshake...");

    // MTK BROM 4-byte start command — each byte echoed as its bitwise NOT
    // Sequence: A0→5F, 0A→F5, 50→AF, 05→FA
    constexpr uint8_t startCmd[] = { 0xA0, 0x0A, 0x50, 0x05 };
    constexpr int MAX_ATTEMPTS = 100;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        // Flush any stale data in the buffer
        m_transport->read(256, 10);

        // Send 0xA0 and wait for 0x5F
        QByteArray syncByte(1, static_cast<char>(startCmd[0]));
        m_transport->write(syncByte);

        QByteArray resp = m_transport->read(1, 100);
        if (resp.isEmpty() || static_cast<uint8_t>(resp[0]) != static_cast<uint8_t>(~startCmd[0] & 0xFF)) {
            QThread::msleep(50);
            continue;  // Retry from scratch
        }

        // Got 0x5F — send remaining 3 bytes (0x0A, 0x50, 0x05)
        bool ok = true;
        for (int k = 1; k < 4; ++k) {
            QByteArray byte(1, static_cast<char>(startCmd[k]));
            m_transport->write(byte);

            QByteArray r = m_transport->read(1, 200);
            uint8_t expected = ~startCmd[k] & 0xFF;
            if (r.isEmpty() || static_cast<uint8_t>(r[0]) != expected) {
                LOG_WARNING_CAT(LOG_TAG, QString("Handshake byte %1 mismatch (attempt %2), retrying")
                                             .arg(k).arg(attempt + 1));
                ok = false;
                break;
            }
        }

        if (ok) {
            LOG_INFO_CAT(LOG_TAG, QString("BROM 4-byte handshake complete (attempt %1)").arg(attempt + 1));
            return true;
        }
        // Partial sync failed — retry entire sequence
        QThread::msleep(50);
    }

    LOG_ERROR_CAT(LOG_TAG, "BROM handshake failed after max attempts");
    return false;
}

// ── Identity queries ────────────────────────────────────────────────────────

uint16_t BromClient::getHwCode()
{
    if (!sendCommand(MtkBromCmd::CMD_GET_HW_CODE))
        return 0;

    uint16_t code = static_cast<uint16_t>(recvWord() >> 16);
    expectStatus();
    LOG_INFO_CAT(LOG_TAG, QString("HW code: 0x%1").arg(code, 4, 16, QChar('0')));
    return code;
}

uint8_t BromClient::getBlVer()
{
    if (!sendCommand(MtkBromCmd::CMD_GET_BL_VER))
        return 0;

    QByteArray resp = m_transport->readExact(1, DEFAULT_TIMEOUT);
    if (resp.isEmpty()) return 0;

    uint8_t ver = static_cast<uint8_t>(resp[0]);
    LOG_INFO_CAT(LOG_TAG, QString("BL version: 0x%1 (%2)")
                              .arg(ver, 2, 16, QChar('0'))
                              .arg(ver == 0xFE ? "BROM mode" : "Preloader mode"));
    return ver;
}

uint16_t BromClient::getBromVersion()
{
    if (!sendCommand(MtkBromCmd::CMD_GET_VERSION))
        return 0;

    uint16_t ver = static_cast<uint16_t>(recvWord() >> 16);
    expectStatus();
    LOG_INFO_CAT(LOG_TAG, QString("BROM version: 0x%1").arg(ver, 4, 16, QChar('0')));
    return ver;
}

bool BromClient::disableWatchdog(uint32_t wdtAddr, uint32_t wdtValue)
{
    LOG_INFO_CAT(LOG_TAG, QString("Disabling watchdog at 0x%1").arg(wdtAddr, 8, 16, QChar('0')));
    return write32(wdtAddr, { wdtValue });
}

MtkDeviceInfo BromClient::getDeviceInfo()
{
    MtkDeviceInfo info;
    info.hwCode    = getHwCode();
    info.blVer     = getBlVer();
    info.isBromMode = (info.blVer == 0xFE);  // 0xFE = BROM, else Preloader

    // Disable watchdog to prevent device reset during operations
    disableWatchdog();

    info.targetCfg = getTargetConfig();
    info.meId      = getMeId();
    info.socId     = getSocId();

    // HW / SW version via CMD_GET_HW_SW_VER
    if (sendCommand(MtkBromCmd::CMD_GET_HW_SW_VER)) {
        info.hwSubCode  = static_cast<uint16_t>(recvWord() >> 16);
        info.hwVersion  = static_cast<uint16_t>(recvWord() >> 16);
        info.swVersion  = static_cast<uint16_t>(recvWord() >> 16);
        expectStatus();
    }

    LOG_INFO_CAT(LOG_TAG, QString("Device: HW=0x%1 BL=0x%2 mode=%3")
                              .arg(info.hwCode, 4, 16, QChar('0'))
                              .arg(info.blVer, 2, 16, QChar('0'))
                              .arg(info.isBromMode ? "BROM" : "Preloader"));
    return info;
}

MtkTargetConfig BromClient::getTargetConfig()
{
    MtkTargetConfig cfg;

    if (!sendCommand(MtkBromCmd::CMD_GET_TARGET_CFG))
        return cfg;

    uint32_t flags = recvWord();
    cfg.configFlags       = flags;
    cfg.secureBootEnabled = (flags & 0x01) != 0;
    cfg.slaEnabled        = (flags & 0x02) != 0;
    cfg.daaEnabled        = (flags & 0x04) != 0;
    cfg.sbc               = (flags & 0x08) != 0;
    expectStatus();

    LOG_INFO_CAT(LOG_TAG, QString("Target config: SBC=%1 SLA=%2 DAA=%3")
                              .arg(cfg.secureBootEnabled)
                              .arg(cfg.slaEnabled)
                              .arg(cfg.daaEnabled));
    return cfg;
}

QByteArray BromClient::getMeId()
{
    if (!sendCommand(MtkBromCmd::CMD_GET_ME_ID))
        return {};

    uint32_t len = recvWord();
    if (len == 0 || len > 256) {
        LOG_ERROR_CAT(LOG_TAG, QString("Invalid ME-ID length: %1").arg(len));
        return {};
    }
    QByteArray meId = echoRead(static_cast<int>(len));
    expectStatus();
    return meId;
}

QByteArray BromClient::getSocId()
{
    if (!sendCommand(MtkBromCmd::CMD_GET_SOC_ID))
        return {};

    uint32_t len = recvWord();
    if (len == 0 || len > 256) {
        LOG_ERROR_CAT(LOG_TAG, QString("Invalid SOC-ID length: %1").arg(len));
        return {};
    }
    QByteArray socId = echoRead(static_cast<int>(len));
    expectStatus();
    return socId;
}

// ── DA transfer ─────────────────────────────────────────────────────────────

bool BromClient::sendDa(const QByteArray& data, uint32_t loadAddr, uint32_t sigLen)
{
    LOG_INFO_CAT(LOG_TAG, QString("Sending DA: %1 bytes to 0x%2")
                              .arg(data.size())
                              .arg(loadAddr, 8, 16, QChar('0')));

    if (!sendCommand(MtkBromCmd::CMD_SEND_DA))
        return false;

    // Send parameters (echoed): load address, total length, signature length
    sendWord(loadAddr);
    sendWord(static_cast<uint32_t>(data.size()));
    sendWord(sigLen);

    if (!expectStatus(MtkBromCmd::STATUS_CONT))
        return false;

    // Stream the DA payload in 4 KiB blocks (raw write, no echo expected)
    constexpr int BLOCK_SIZE = 4096;
    qint64 totalSent = 0;
    const qint64 totalSize = data.size();

    while (totalSent < totalSize) {
        int chunkLen = static_cast<int>(qMin<qint64>(BLOCK_SIZE, totalSize - totalSent));
        QByteArray chunk = data.mid(static_cast<int>(totalSent), chunkLen);

        if (m_transport->write(chunk) != chunkLen) {
            LOG_ERROR_CAT(LOG_TAG, "DA transfer failed during payload send");
            return false;
        }

        totalSent += chunkLen;
        emit transferProgress(totalSent, totalSize);
    }

    // Read the device's checksum and compare
    uint16_t checksum = MtkChecksum::compute(data);
    uint16_t devChecksum = readStatus();

    if (checksum != devChecksum) {
        LOG_ERROR_CAT(LOG_TAG, QString("DA checksum mismatch: local=0x%1 remote=0x%2")
                                   .arg(checksum, 4, 16, QChar('0'))
                                   .arg(devChecksum, 4, 16, QChar('0')));
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, "DA checksum OK");
    return expectStatus();
}

bool BromClient::jumpDa(uint32_t addr)
{
    LOG_INFO_CAT(LOG_TAG, QString("Jump DA to 0x%1").arg(addr, 8, 16, QChar('0')));

    if (!sendCommand(MtkBromCmd::CMD_JUMP_DA))
        return false;

    sendWord(addr);
    return expectStatus();
}

// ── Security ────────────────────────────────────────────────────────────────

bool BromClient::sendCert(const QByteArray& certData)
{
    LOG_INFO_CAT(LOG_TAG, QString("Sending certificate (%1 bytes)").arg(certData.size()));

    if (!sendCommand(MtkBromCmd::CMD_SEND_CERT))
        return false;

    sendWord(static_cast<uint32_t>(certData.size()));

    if (!expectStatus(MtkBromCmd::STATUS_CONT))
        return false;

    echoWrite(certData);
    return expectStatus();
}

bool BromClient::sendAuth(const QByteArray& authData)
{
    LOG_INFO_CAT(LOG_TAG, QString("Sending auth data (%1 bytes)").arg(authData.size()));

    if (!sendCommand(MtkBromCmd::CMD_SEND_AUTH))
        return false;

    sendWord(static_cast<uint32_t>(authData.size()));

    if (!expectStatus(MtkBromCmd::STATUS_CONT))
        return false;

    echoWrite(authData);
    return expectStatus();
}

// ── Low-level memory access ─────────────────────────────────────────────────

QByteArray BromClient::read32(uint32_t addr, uint32_t count)
{
    if (!sendCommand(MtkBromCmd::CMD_READ32))
        return {};

    sendWord(addr);
    sendWord(count);

    if (!expectStatus(MtkBromCmd::STATUS_CONT))
        return {};

    QByteArray result = echoRead(static_cast<int>(count * 4));
    expectStatus();
    return result;
}

bool BromClient::write32(uint32_t addr, const QList<uint32_t>& values)
{
    if (!sendCommand(MtkBromCmd::CMD_WRITE32))
        return false;

    sendWord(addr);
    sendWord(static_cast<uint32_t>(values.size()));

    if (!expectStatus(MtkBromCmd::STATUS_CONT))
        return false;

    for (uint32_t v : values)
        sendWord(v);

    return expectStatus();
}

// ── PMIC (power management) ─────────────────────────────────────────────────

bool BromClient::i2cInit()
{
    return sendCommand(MtkBromCmd::CMD_I2C_INIT) && expectStatus();
}

bool BromClient::pwrInit()
{
    return sendCommand(MtkBromCmd::CMD_PWR_INIT) && expectStatus();
}

bool BromClient::pwrDeinit()
{
    return sendCommand(MtkBromCmd::CMD_PWR_DEINIT) && expectStatus();
}

uint16_t BromClient::pwrRead16(uint16_t addr)
{
    if (!sendCommand(MtkBromCmd::CMD_PWR_READ16))
        return 0;

    // Address as 32-bit
    sendWord(static_cast<uint32_t>(addr));
    uint16_t val = static_cast<uint16_t>(recvWord());
    expectStatus();
    return val;
}

bool BromClient::pwrWrite16(uint16_t addr, uint16_t value)
{
    if (!sendCommand(MtkBromCmd::CMD_PWR_WRITE16))
        return false;

    sendWord(static_cast<uint32_t>(addr));
    sendWord(static_cast<uint32_t>(value));
    return expectStatus();
}

// ── Private helpers (BROM echo protocol) ────────────────────────────────────
// In the MTK BROM echo protocol, every command byte and parameter word sent
// by the host is echoed back by the device. We MUST read the echo to keep
// the buffer in sync. See mtkclient Port.py echo() method.

bool BromClient::sendCommand(uint8_t cmd)
{
    QByteArray pkt(1, static_cast<char>(cmd));
    if (m_transport->write(pkt) != 1)
        return false;

    // Read the echo byte
    QByteArray echo = m_transport->readExact(1, DEFAULT_TIMEOUT);
    if (echo.isEmpty()) {
        LOG_ERROR_CAT(LOG_TAG, QString("No echo for command 0x%1").arg(cmd, 2, 16, QChar('0')));
        return false;
    }
    if (static_cast<uint8_t>(echo[0]) != cmd) {
        LOG_ERROR_CAT(LOG_TAG, QString("Echo mismatch for command: sent 0x%1, got 0x%2")
                                   .arg(cmd, 2, 16, QChar('0'))
                                   .arg(static_cast<uint8_t>(echo[0]), 2, 16, QChar('0')));
        return false;
    }
    return true;
}

bool BromClient::expectStatus(uint16_t expected)
{
    uint16_t status = readStatus();
    if (status != expected) {
        LOG_ERROR_CAT(LOG_TAG, QString("Unexpected status: 0x%1 (expected 0x%2)")
                                   .arg(status, 4, 16, QChar('0'))
                                   .arg(expected, 4, 16, QChar('0')));
        return false;
    }
    return true;
}

QByteArray BromClient::echoRead(int size, int timeoutMs)
{
    return m_transport->readExact(size, timeoutMs);
}

bool BromClient::echoWrite(const QByteArray& data)
{
    if (m_transport->write(data) != data.size())
        return false;

    // Read back the echo — device echoes cert/auth data byte-for-byte.
    // (DA bulk payload is sent via raw write() separately, not echoWrite.)
    QByteArray echo = m_transport->readExact(data.size(), DEFAULT_TIMEOUT);
    if (echo.size() != data.size()) {
        LOG_WARNING_CAT(LOG_TAG, QString("echoWrite: expected %1 echo bytes, got %2")
                                     .arg(data.size()).arg(echo.size()));
        return false;
    }
    return true;
}

uint16_t BromClient::readStatus()
{
    QByteArray resp = m_transport->readExact(2, DEFAULT_TIMEOUT);
    if (resp.size() < 2)
        return 0xFFFF;
    return qFromBigEndian<uint16_t>(reinterpret_cast<const uchar*>(resp.constData()));
}

void BromClient::sendWord(uint32_t value)
{
    uint32_t be = qToBigEndian(value);
    QByteArray data(reinterpret_cast<const char*>(&be), 4);
    m_transport->write(data);

    // Read the echo (BROM echoes every word back)
    QByteArray echo = m_transport->readExact(4, DEFAULT_TIMEOUT);
    if (echo.size() == 4 && echo != data) {
        LOG_WARNING_CAT(LOG_TAG, QString("sendWord echo mismatch: sent 0x%1, got 0x%2")
                                     .arg(value, 8, 16, QChar('0'))
                                     .arg(qFromBigEndian<uint32_t>(reinterpret_cast<const uchar*>(echo.constData())), 8, 16, QChar('0')));
    }
}

uint32_t BromClient::recvWord()
{
    QByteArray resp = m_transport->readExact(4, DEFAULT_TIMEOUT);
    if (resp.size() < 4)
        return 0;
    return qFromBigEndian<uint32_t>(reinterpret_cast<const uchar*>(resp.constData()));
}

} // namespace sakura
