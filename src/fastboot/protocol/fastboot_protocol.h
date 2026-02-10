#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace sakura {

// ---------------------------------------------------------------------------
// Fastboot text-based protocol
//
// Commands are sent as plain ASCII strings.  Responses begin with a 4-byte
// status prefix: "OKAY", "FAIL", "DATA", or "INFO".  The remainder of the
// response is the payload / message.
// ---------------------------------------------------------------------------

enum class FastbootResponseType {
    Okay,       // "OKAY" – command succeeded; optional data follows
    Fail,       // "FAIL" – command failed; error string follows
    Data,       // "DATA" – host should send <length> bytes of data
    Info,       // "INFO" – informational message (may repeat)
    Unknown     // Unrecognised prefix
};

struct FastbootResponse {
    FastbootResponseType type = FastbootResponseType::Unknown;
    QByteArray           data;   // Everything after the 4-byte prefix

    bool isOkay() const { return type == FastbootResponseType::Okay; }
    bool isFail() const { return type == FastbootResponseType::Fail; }
    bool isData() const { return type == FastbootResponseType::Data; }
    bool isInfo() const { return type == FastbootResponseType::Info; }

    /// DATA responses encode the expected download size as a hex string.
    uint32_t dataLength() const;

    /// Human-readable description.
    QString toString() const;
};

// ---------------------------------------------------------------------------
// FastbootProtocol – static helpers for the Fastboot wire format
// ---------------------------------------------------------------------------

class FastbootProtocol {
public:
    // --- Response prefixes (4 bytes, no null terminator) ---
    static constexpr const char* PREFIX_OKAY = "OKAY";
    static constexpr const char* PREFIX_FAIL = "FAIL";
    static constexpr const char* PREFIX_DATA = "DATA";
    static constexpr const char* PREFIX_INFO = "INFO";

    // --- Known vendor USB VIDs ---
    static constexpr uint16_t VID_GOOGLE   = 0x18D1;
    static constexpr uint16_t VID_SAMSUNG  = 0x04E8;
    static constexpr uint16_t VID_XIAOMI   = 0x2717;
    static constexpr uint16_t VID_HUAWEI   = 0x12D1;
    static constexpr uint16_t VID_ONEPLUS  = 0x2A70;
    static constexpr uint16_t VID_MOTOROLA = 0x22B8;
    static constexpr uint16_t VID_SONY     = 0x0FCE;
    static constexpr uint16_t VID_LG       = 0x1004;
    static constexpr uint16_t VID_OPPO     = 0x22D9;
    static constexpr uint16_t VID_QUALCOMM = 0x05C6;

    // Standard Fastboot PID (used by most vendors in bootloader mode)
    static constexpr uint16_t PID_FASTBOOT = 0xD00D;

    // Maximum single-packet payload size for Fastboot USB transfers
    static constexpr uint32_t MAX_DOWNLOAD_SIZE_DEFAULT = 256 * 1024 * 1024; // 256 MiB

    // Maximum command string length (protocol limit)
    static constexpr int MAX_COMMAND_LENGTH = 64;

    // --- Command building ---------------------------------------------------

    /// Build a plain command packet, e.g. "getvar:version".
    static QByteArray buildCommand(const QString& cmd,
                                   const QStringList& args = {});

    /// Build a download command with hex-encoded size:  "download:0x<hex>"
    static QByteArray buildDownloadCommand(uint32_t size);

    // --- Response parsing ----------------------------------------------------

    /// Parse a raw response packet from the device.
    static FastbootResponse parseResponse(const QByteArray& data);

    /// Return the human-readable name for a response type.
    static QString responseTypeName(FastbootResponseType type);

    // --- VID helpers ---------------------------------------------------------

    /// List of all known fastboot VIDs (for device enumeration).
    static QList<uint16_t> knownVids();

    /// Check whether a VID belongs to a known fastboot vendor.
    static bool isKnownFastbootVid(uint16_t vid);

private:
    FastbootProtocol() = delete; // all-static class
};

} // namespace sakura
