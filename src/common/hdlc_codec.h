#pragma once

#include <QByteArray>
#include <cstdint>

namespace sakura {

// HDLC framing used by Qualcomm Diag and Spreadtrum FDL protocols
class HdlcCodec {
public:
    static constexpr uint8_t FLAG = 0x7E;
    static constexpr uint8_t ESCAPE = 0x7D;
    static constexpr uint8_t ESCAPE_XOR = 0x20;

    // Encode data with HDLC framing (flag + escaped payload + CRC16 + flag)
    static QByteArray encode(const QByteArray& data, bool useCrc = true);

    // Decode HDLC frame (removes flags, unescapes, validates CRC)
    static QByteArray decode(const QByteArray& frame, bool validateCrc = true);

    // Extract complete HDLC frames from a data stream
    static QList<QByteArray> extractFrames(const QByteArray& data);

    // Escape a byte sequence (without adding flags)
    static QByteArray escape(const QByteArray& data);

    // Unescape a byte sequence
    static QByteArray unescape(const QByteArray& data);
};

// Spreadtrum-specific HDLC variant
class SprdHdlc {
public:
    // Encode with Spreadtrum HDLC framing (big-endian type+length header, checksum)
    static QByteArray encode(uint16_t type, const QByteArray& payload, bool transcode = true);

    // Decode Spreadtrum HDLC frame
    struct Frame {
        uint16_t type = 0;
        QByteArray payload;
        bool valid = false;
    };
    static Frame decode(const QByteArray& data, bool transcode = true);
};

} // namespace sakura
