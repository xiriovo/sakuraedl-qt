#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstdint>

namespace sakura {

class ITransport;

// ── BROM echo-protocol command codes ──
namespace MtkBromCmd {
    constexpr uint8_t CMD_GET_HW_CODE    = 0xFD;
    constexpr uint8_t CMD_GET_BL_VER    = 0xFE;  // 0xFE in response = BROM mode
    constexpr uint8_t CMD_GET_VERSION   = 0xFF;
    constexpr uint8_t CMD_GET_HW_SW_VER  = 0xFC;
    constexpr uint8_t CMD_GET_HW_DICT    = 0xA1;
    constexpr uint8_t CMD_SEND_DA        = 0xD7;
    constexpr uint8_t CMD_JUMP_DA        = 0xD5;
    constexpr uint8_t CMD_SEND_CERT      = 0xE0;
    constexpr uint8_t CMD_GET_ME_ID      = 0xE1;
    constexpr uint8_t CMD_GET_SOC_ID     = 0xE7;
    constexpr uint8_t CMD_GET_TARGET_CFG = 0xD8;
    constexpr uint8_t CMD_SEND_AUTH      = 0xE2;
    constexpr uint8_t CMD_I2C_INIT       = 0xB0;
    constexpr uint8_t CMD_PWR_INIT       = 0xC4;
    constexpr uint8_t CMD_PWR_DEINIT     = 0xC5;
    constexpr uint8_t CMD_PWR_READ16     = 0xC6;
    constexpr uint8_t CMD_PWR_WRITE16    = 0xC7;
    constexpr uint8_t CMD_READ16         = 0xA2;
    constexpr uint8_t CMD_READ32         = 0xD1;
    constexpr uint8_t CMD_WRITE16        = 0xA4;
    constexpr uint8_t CMD_WRITE32        = 0xD4;

    constexpr uint16_t STATUS_OK         = 0x0000;
    constexpr uint16_t STATUS_CONT       = 0x0069;
}

// ── Target configuration flags ──
struct MtkTargetConfig {
    bool secureBootEnabled = false;
    bool slaEnabled = false;
    bool daaEnabled = false;
    bool sbc = false;
    uint32_t configFlags = 0;
};

// ── Device identity block ──
struct MtkDeviceInfo {
    QString comPort;
    uint16_t hwCode = 0;
    uint16_t hwSubCode = 0;
    uint16_t hwVersion = 0;
    uint16_t swVersion = 0;
    uint8_t  blVer = 0;       // 0xFE = BROM mode, else preloader
    bool     isBromMode = false;
    QByteArray meId;          // 16 bytes
    QByteArray socId;         // 32 bytes
    MtkTargetConfig targetCfg;
};

// ── BROM client — implements the boot-ROM echo protocol ──
class BromClient : public QObject {
    Q_OBJECT

public:
    explicit BromClient(ITransport* transport, QObject* parent = nullptr);
    ~BromClient() override;

    // Core handshake sequence (4-byte sync: A0 0A 50 05 → 5F F5 AF FA)
    bool handshake();

    // Identity queries
    uint16_t getHwCode();
    uint8_t getBlVer();           // 0xFE = BROM mode, else preloader
    uint16_t getBromVersion();
    MtkDeviceInfo getDeviceInfo();
    MtkTargetConfig getTargetConfig();
    QByteArray getMeId();
    QByteArray getSocId();

    // Watchdog control
    bool disableWatchdog(uint32_t wdtAddr = 0x10007000, uint32_t wdtValue = 0x22000000);

    // DA transfer
    bool sendDa(const QByteArray& data, uint32_t loadAddr, uint32_t sigLen = 0);
    bool jumpDa(uint32_t addr);

    // Security
    bool sendCert(const QByteArray& certData);
    bool sendAuth(const QByteArray& authData);

    // Low-level memory access
    QByteArray read32(uint32_t addr, uint32_t count = 1);
    bool write32(uint32_t addr, const QList<uint32_t>& values);

    // Power management IC access
    bool i2cInit();
    bool pwrInit();
    bool pwrDeinit();
    uint16_t pwrRead16(uint16_t addr);
    bool pwrWrite16(uint16_t addr, uint16_t value);

signals:
    void transferProgress(qint64 sent, qint64 total);

private:
    bool sendCommand(uint8_t cmd);
    bool expectStatus(uint16_t expected = MtkBromCmd::STATUS_OK);
    QByteArray echoRead(int size, int timeoutMs = 5000);
    bool echoWrite(const QByteArray& data);
    uint16_t readStatus();
    void sendWord(uint32_t value);
    uint32_t recvWord();

    ITransport* m_transport = nullptr;
    static constexpr int DEFAULT_TIMEOUT = 5000;
};

} // namespace sakura
