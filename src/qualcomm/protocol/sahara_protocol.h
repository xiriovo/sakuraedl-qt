#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstdint>
#include <functional>

namespace sakura {

class ITransport;

// ─── Sahara command IDs (per edl2 reference, exactly matching) ───────
// edl2: SakuraEDL.Qualcomm.Protocol.SaharaCommand
enum class SaharaCommand : uint32_t {
    Hello              = 0x01,
    HelloResponse      = 0x02,
    ReadData           = 0x03,  // 32-bit read (old devices)
    EndImageTransfer   = 0x04,
    Done               = 0x05,
    DoneResponse       = 0x06,
    Reset              = 0x07,  // Hard reset (restart device)
    ResetResponse      = 0x08,
    MemoryDebug        = 0x09,
    MemoryRead         = 0x0A,
    CommandReady       = 0x0B,  // Device → Host: Command mode accepted
    SwitchMode         = 0x0C,  // Host → Device: Switch mode
    Execute            = 0x0D,  // Host → Device: Execute command
    ExecuteData        = 0x0E,  // Device → Host: Command data response
    ExecuteResponse    = 0x0F,  // Host → Device: Command response confirm
    MemoryDebug64      = 0x10,
    MemoryRead64       = 0x11,
    ReadData64         = 0x12,  // 64-bit read (new devices)
    ResetStateMachine  = 0x13,  // Soft reset (state machine reset)
};

// ─── Sahara mode codes ───────────────────────────────────────────────
enum class SaharaMode : uint32_t {
    ImageTransferPending  = 0x0,
    ImageTransferComplete = 0x1,
    MemoryDebug           = 0x2,
    Command               = 0x3,  // Command mode (read info)
};

// ─── Sahara executive commands (per edl2 reference) ──────────────────
// edl2: SakuraEDL.Qualcomm.Protocol.SaharaExecCommand
enum class SaharaExecCommand : uint32_t {
    SerialNumRead   = 0x01,  // Serial number
    MsmHwIdRead     = 0x02,  // HWID (V1/V2 ONLY)
    OemPkHashRead   = 0x03,  // PK Hash
    SblInfoRead     = 0x06,  // SBL info (V3 ONLY)
    SblSwVersion    = 0x07,  // SBL version (V1/V2 ONLY)
    PblSwVersion    = 0x08,  // PBL version
    ChipIdV3Read    = 0x0A,  // V3 chip info (includes HWID)
    SerialNumRead64 = 0x14,  // 64-bit serial number
};

// ─── Sahara status codes ─────────────────────────────────────────────
enum class SaharaStatus : uint32_t {
    Success             = 0x00,
    InvalidCommand      = 0x01,
    ProtocolMismatch    = 0x02,
    InvalidTargetProto  = 0x03,
    InvalidHostProto    = 0x04,
    InvalidPacketSize   = 0x05,
    UnexpectedImageId   = 0x06,
    InvalidHeaderSize   = 0x07,
    InvalidDataSize     = 0x08,
    InvalidImageType    = 0x09,
    InvalidTxLength     = 0x0A,
    InvalidRxLength     = 0x0B,
    GeneralTxError      = 0x0C,
    GeneralRxError      = 0x0D,
    CmdSwitchFailed     = 0x12,
    InvalidMode         = 0x18,
    ExecCmdFailed       = 0x1C,
    ExecDataInvalid     = 0x1D,
    HashMismatch        = 0x1E,
    HashUnsupported     = 0x1F,
};

// ─── Device info gathered from Sahara ────────────────────────────────
struct SaharaDeviceInfo {
    uint32_t saharaVersion = 0;
    uint32_t saharaMinVersion = 0;
    uint32_t msmId         = 0;
    QByteArray pkHash;
    QString pkHashHex;         // lowercase hex string
    uint32_t oemId         = 0;
    uint32_t modelId       = 0;
    uint32_t serial        = 0;
    QString serialHex;
    QString chipName;
    QString hwIdHex;           // Full HWID hex string (edl2 format)
    QString vendor;            // Vendor from OEM ID
    uint32_t sblVersion    = 0;
    QByteArray hwIdRaw;        // Full HW ID raw bytes
    QByteArray serialRaw;      // Raw serial data
    QByteArray sblInfoRaw;     // Raw SBL info data
    bool chipInfoRead      = false;
};

// ─── Sahara packet structures (on-wire, little-endian) ───────────────
#pragma pack(push, 1)

struct SaharaPacketHeader {
    uint32_t command = 0;
    uint32_t length  = 0;
};

struct SaharaHelloPacket {
    SaharaPacketHeader header;   // command = 0x01
    uint32_t version     = 0;
    uint32_t versionMin  = 0;
    uint32_t maxCmdLen   = 0;
    uint32_t mode        = 0;
    uint32_t reserved[6] = {};
};

struct SaharaHelloResponsePacket {
    SaharaPacketHeader header;   // command = 0x02
    uint32_t version     = 0;
    uint32_t versionMin  = 0;
    uint32_t status      = 0;
    uint32_t mode        = 0;
    uint32_t reserved[6] = {};
};

struct SaharaReadDataPacket {
    SaharaPacketHeader header;   // command = 0x03
    uint32_t imageId = 0;
    uint32_t offset  = 0;
    uint32_t length  = 0;
};

struct SaharaReadData64Packet {
    SaharaPacketHeader header;   // command = 0x12
    uint64_t imageId = 0;
    uint64_t offset  = 0;
    uint64_t length  = 0;
};

struct SaharaEndImageTransferPacket {
    SaharaPacketHeader header;   // command = 0x04
    uint32_t imageId = 0;
    uint32_t status  = 0;
};

struct SaharaDonePacket {
    SaharaPacketHeader header;   // command = 0x05
};

struct SaharaDoneResponsePacket {
    SaharaPacketHeader header;   // command = 0x06
    uint32_t imageTxStatus = 0;
};

struct SaharaResetPacket {
    SaharaPacketHeader header;   // command = 0x07
};

// Execute: Host → Device (0x0D)
struct SaharaExecutePacket {
    SaharaPacketHeader header;   // command = 0x0D
    uint32_t clientCommand = 0;
};

// ExecuteData: Device → Host (0x0E) — data response header
struct SaharaExecuteDataResponsePacket {
    SaharaPacketHeader header;   // command = 0x0E
    uint32_t clientCommand = 0;
    uint32_t dataLength    = 0;
};

// ExecuteResponse: Host → Device (0x0F) — confirm, request actual data
struct SaharaExecuteResponsePacket {
    SaharaPacketHeader header;   // command = 0x0F
    uint32_t clientCommand = 0;
};

struct SaharaSwitchModePacket {
    SaharaPacketHeader header;   // command = 0x0C
    uint32_t mode = 0;
};

#pragma pack(pop)

// ─── Sahara client ───────────────────────────────────────────────────
class SaharaClient : public QObject {
    Q_OBJECT

public:
    explicit SaharaClient(ITransport* transport, QObject* parent = nullptr);

    // Main handshake — reads Hello, optionally reads chip info, sends HelloResponse
    bool handshakeAsync(SaharaMode requestedMode = SaharaMode::ImageTransferPending);

    // Get device info (populated during handshake if Command mode was available)
    SaharaDeviceInfo getDeviceInfo() const { return m_deviceInfo; }

    // Upload a programmer / loader image to the device
    bool uploadLoader(const QByteArray& loaderData);

    // Send reset commands
    bool sendReset();           // Hard reset (0x07)
    bool sendResetStateMachine(); // Soft reset (0x13) — device resends Hello

    // Read chip info via exec commands (requires Command mode)
    QByteArray readChipInfo(SaharaExecCommand cmd);

    // Protocol version detected on the device
    uint32_t deviceSaharaVersion() const { return m_deviceVersion; }

signals:
    void uploadProgress(qint64 sent, qint64 total);
    void statusMessage(const QString& message);

private:
    QByteArray readPacket(int timeoutMs = 5000);
    bool sendPacket(const void* data, uint32_t size);
    void sendHelloResponse(SaharaMode mode);
    void sendSwitchMode(SaharaMode mode);
    bool tryReadChipInfo();
    void readChipInfoV1V2();
    void readChipInfoV3();
    QByteArray executeCommandSafe(SaharaExecCommand cmd);
    void parseHwIdV1V2(const QByteArray& data);
    void parseV3ExtendedInfo(const QByteArray& data);
    void parseSblInfo(const QByteArray& data);
    void parseSerial(const QByteArray& data);

    ITransport* m_transport = nullptr;
    uint32_t m_deviceVersion = 0;
    uint32_t m_deviceMinVersion = 0;
    uint32_t m_maxCmdLen = 0;
    SaharaMode m_currentMode = SaharaMode::ImageTransferPending;
    SaharaDeviceInfo m_deviceInfo;
    bool m_chipInfoAttempted = false;
    bool m_skipCommandMode = false;

    static constexpr uint32_t SAHARA_VERSION = 2;
    static constexpr uint32_t SAHARA_VERSION_MIN = 1;
    static constexpr int READ_TIMEOUT_MS = 30000;
    static constexpr int HELLO_TIMEOUT_MS = 60000;
    static constexpr int UPLOAD_TIMEOUT_MS = 30000;
    static constexpr int CMD_TIMEOUT_MS = 5000;
};

} // namespace sakura
