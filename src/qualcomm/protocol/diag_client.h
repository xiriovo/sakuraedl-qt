#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstdint>

namespace sakura {

class ITransport;

// ─── Qualcomm NV item IDs ───────────────────────────────────────────
enum class NvItem : uint32_t {
    NV_ESN          = 0,       // Electronic Serial Number
    NV_IMEI         = 550,     // IMEI (International Mobile Equipment Identity)
    NV_MEID         = 4678,    // Mobile Equipment Identifier
    NV_SPC          = 85,      // Service Programming Code
    NV_OEM_LOCK     = 7121,    // OEM lock status
    NV_BAND_PREF    = 441,     // Band preference
    NV_LTE_BAND     = 65633,   // LTE band configuration
};

// ─── Diag command IDs ────────────────────────────────────────────────
enum class DiagCommand : uint8_t {
    VERNO           = 0x00,    // Version number
    STATUS          = 0x0C,    // Status
    SECURITY_FREEZE = 0x15,    // Security freeze
    NV_READ         = 0x26,    // NV item read
    NV_WRITE        = 0x27,    // NV item write
    SPC             = 0x41,    // SPC validation
    SUBSYS          = 0x4B,    // Subsystem dispatch
    NV_READ_EXT     = 0x85,    // Extended NV read
    NV_WRITE_EXT    = 0x86,    // Extended NV write
    PASSWORD        = 0x46,    // Security password
    DLOAD           = 0x3A,    // Switch to download mode
    LOG_CONFIG      = 0x73,    // Log config
    EFS_OPEN        = 0x4B,    // EFS2 commands via subsystem
    REBOOT          = 0x29,    // Reboot (mode reset)
};

// ─── Diag subsystem IDs ─────────────────────────────────────────────
enum class DiagSubsys : uint8_t {
    DIAG            = 0x12,
    NAS             = 0x43,
    GPS             = 0x0D,
    FS              = 0x13,    // EFS2
};

// ─── NV operation status ─────────────────────────────────────────────
enum class NvStatus : uint16_t {
    Done            = 0,
    Busy            = 1,
    BadParm         = 2,
    ReadOnly        = 3,
    BadCmd          = 4,
    MemFull         = 5,
    Fail            = 6,
    Inactive        = 7,
    BadLen          = 8,
};

// ─── Device info from Diag ──────────────────────────────────────────
struct DiagDeviceInfo {
    QString esn;
    QString meid;
    QString swVersion;
    QString compDate;
    QString compTime;
    QString modelId;
};

// ─── IMEI info ──────────────────────────────────────────────────────
struct ImeiInfo {
    QString imei1;
    QString imei2;
    bool valid = false;
};

// ─── Diag packet structures ─────────────────────────────────────────
#pragma pack(push, 1)

struct DiagNvReadRequest {
    uint8_t  cmd = static_cast<uint8_t>(DiagCommand::NV_READ);
    uint16_t item = 0;
    uint8_t  data[128] = {};
    uint16_t status = 0;
};

struct DiagNvWriteRequest {
    uint8_t  cmd = static_cast<uint8_t>(DiagCommand::NV_WRITE);
    uint16_t item = 0;
    uint8_t  data[128] = {};
    uint16_t status = 0;
};

struct DiagSpcRequest {
    uint8_t cmd = static_cast<uint8_t>(DiagCommand::SPC);
    uint8_t spc[6] = {};  // 6-digit SPC as ASCII
};

struct DiagSpcResponse {
    uint8_t cmd = 0;
    uint8_t status = 0;   // 1 = success
};

struct DiagVersionResponse {
    uint8_t  cmd = 0;
    char     compDate[11] = {};
    char     compTime[8] = {};
    char     relDate[11] = {};
    char     relTime[8] = {};
    char     verDir[8] = {};
    uint8_t  scm = 0;
    uint8_t  mobModel = 0;
    uint8_t  mobFirmRev = 0;
    uint8_t  slotCyclIdx = 0;
    uint8_t  vocMaj = 0;
    uint8_t  vocMin = 0;
};

#pragma pack(pop)

// ─── Diag client ─────────────────────────────────────────────────────
class DiagClient : public QObject {
    Q_OBJECT

public:
    explicit DiagClient(ITransport* transport, QObject* parent = nullptr);

    // ── Connection ───────────────────────────────────────────────────
    bool connect();
    void disconnect();
    bool isConnected() const { return m_connected; }

    // ── SPC / Security ───────────────────────────────────────────────
    bool sendSpc(const QString& code = "000000");
    bool sendPassword(const QString& password);

    // ── NV operations ────────────────────────────────────────────────
    QByteArray readNv(uint16_t item);
    bool writeNv(uint16_t item, const QByteArray& data);

    // ── IMEI ─────────────────────────────────────────────────────────
    ImeiInfo readImei();
    bool writeImei(const QString& imei1, const QString& imei2 = QString());

    // ── Device info ──────────────────────────────────────────────────
    DiagDeviceInfo readDeviceInfo();

    // ── QCN (NV backup) ─────────────────────────────────────────────
    bool readQcn(const QString& savePath);

    // ── Mode switching ───────────────────────────────────────────────
    bool switchToDownloadMode();
    bool reboot();

    // ── EFS ──────────────────────────────────────────────────────────
    QByteArray efsRead(const QString& path);

signals:
    void statusMessage(const QString& message);
    void transferProgress(qint64 current, qint64 total);

private:
    QByteArray sendCommand(const QByteArray& payload, int timeoutMs = 3000);
    QByteArray sendRawDiag(DiagCommand cmd, const QByteArray& payload = {});

    // IMEI encoding/decoding helpers
    static QByteArray encodeImei(const QString& imei);
    static QString decodeImei(const QByteArray& data);

    ITransport* m_transport = nullptr;
    bool m_connected = false;
    bool m_spcUnlocked = false;

    static constexpr int DIAG_TIMEOUT_MS = 3000;
    static constexpr int NV_DATA_SIZE = 128;
};

} // namespace sakura
