#pragma once

#include <QByteArray>
#include <cstdint>

#include "common/hdlc_codec.h"

namespace sakura {

// ── Spreadtrum-specific HDLC protocol wrapper ───────────────────────────────
//
// Wraps the SprdHdlc codec from common/ with FDL-specific framing:
//
// Raw packet layout (before HDLC encoding):
//   [type(2, big-endian)][length(2, big-endian)][payload(N)][checksum(2)]
//
// After HDLC encoding:
//   [0x7E][escaped packet][0x7E]
//
// The "transcode" flag controls whether HDLC escape encoding is applied.
// After DISABLE_TRANSCODE command, raw binary is sent without escaping.
//

class SprdHdlcProtocol {
public:
    // Encode a command packet for transmission
    static QByteArray encode(uint16_t type, const QByteArray& payload,
                             bool transcode = true);

    // Decode a received response packet
    static QByteArray decode(const QByteArray& data, bool transcode = true);

    // Extract the type field from a decoded packet
    static uint16_t extractType(const QByteArray& decodedPacket);

    // Extract the payload from a decoded packet (strips type + length + checksum)
    static QByteArray extractPayload(const QByteArray& decodedPacket);

    // Build raw packet (type + length + payload + checksum) without HDLC framing
    static QByteArray buildRawPacket(uint16_t type, const QByteArray& payload);

    // Validate checksum of a raw (un-escaped) packet
    static bool validateChecksum(const QByteArray& rawPacket);

    // Compute Spreadtrum checksum over data
    static uint16_t computeChecksum(const QByteArray& data);

    // Constants
    static constexpr uint8_t HDLC_FLAG = 0x7E;
    static constexpr uint8_t HDLC_ESCAPE = 0x7D;
    static constexpr uint8_t HDLC_ESCAPE_XOR = 0x20;

    // Maximum packet sizes
    static constexpr int MAX_FRAME_SIZE  = 0x2800;  // 10 KiB
    static constexpr int HEADER_SIZE     = 4;        // type(2) + length(2)
    static constexpr int CHECKSUM_SIZE   = 2;
};

} // namespace sakura
