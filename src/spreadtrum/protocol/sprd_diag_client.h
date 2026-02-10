#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstdint>

namespace sakura {

class ITransport;

// ── Spreadtrum Diag protocol ────────────────────────────────────────────────
//
// Used for diagnostics, NV/IMEI read/write, modem configuration, etc.
// Communication uses HDLC framing with Diag-specific command types.
//

namespace SprdDiagCmd {
    // Diag command types
    constexpr uint8_t CMD_CONNECT     = 0x00;
    constexpr uint8_t CMD_READ_NV     = 0x01;
    constexpr uint8_t CMD_WRITE_NV    = 0x02;
    constexpr uint8_t CMD_READ_IMEI   = 0x03;
    constexpr uint8_t CMD_WRITE_IMEI  = 0x04;
    constexpr uint8_t CMD_READ_VERSION = 0x05;
    constexpr uint8_t CMD_READ_PHASE  = 0x06;
    constexpr uint8_t CMD_SET_CALIBRATION = 0x07;
    constexpr uint8_t CMD_RESET       = 0x0A;
    constexpr uint8_t CMD_POWER_OFF   = 0x0B;
    constexpr uint8_t CMD_READ_CHIPID = 0x0C;
    constexpr uint8_t CMD_SIM_LOCK    = 0x0D;
    constexpr uint8_t CMD_SIM_UNLOCK  = 0x0E;
    constexpr uint8_t CMD_READ_BATTERY = 0x10;
    constexpr uint8_t CMD_READ_ADC    = 0x11;

    // Diag sub-command for NV operations
    constexpr uint16_t NV_READ_ITEM   = 0x0001;
    constexpr uint16_t NV_WRITE_ITEM  = 0x0002;
    constexpr uint16_t NV_DELETE_ITEM = 0x0003;

    // Response indicators
    constexpr uint8_t RESP_OK    = 0x00;
    constexpr uint8_t RESP_ERROR = 0xFF;
}

struct SprdPhaseCheck {
    QString   sn;               // Serial number
    QString   station;          // Test station
    uint32_t  flags = 0;
    bool      passed = false;
};

struct SprdNvItem {
    uint16_t  id = 0;
    QByteArray data;
    bool      valid = false;
};

class SprdDiagClient : public QObject {
    Q_OBJECT

public:
    explicit SprdDiagClient(ITransport* transport, QObject* parent = nullptr);
    ~SprdDiagClient() override;

    // Connection
    bool connect();

    // NV operations
    SprdNvItem readNvItem(uint16_t itemId);
    bool writeNvItem(uint16_t itemId, const QByteArray& data);
    bool deleteNvItem(uint16_t itemId);

    // IMEI
    QByteArray readImei(int simSlot = 0);
    bool writeImei(int simSlot, const QByteArray& imei);

    // Device info
    QString readVersion();
    QByteArray readChipId();
    SprdPhaseCheck readPhaseCheck();

    // Control
    bool reset();
    bool powerOff();

    // Calibration mode
    bool enterCalibrationMode();

signals:
    void diagMessage(const QString& message);

private:
    bool sendDiagCommand(uint8_t cmd, const QByteArray& payload = {});
    QByteArray recvDiagResponse(int timeoutMs = 3000);
    bool isDiagOk(const QByteArray& resp) const;

    ITransport* m_transport = nullptr;
    static constexpr int DEFAULT_TIMEOUT = 3000;
};

} // namespace sakura
