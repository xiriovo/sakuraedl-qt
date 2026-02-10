#include "hdlc_codec.h"
#include "crc_utils.h"
#include <QList>

namespace sakura {

QByteArray HdlcCodec::encode(const QByteArray& data, bool useCrc)
{
    QByteArray payload = data;

    // Append CRC16 if needed
    if (useCrc) {
        uint16_t crc = Crc16::ccitt(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
        payload.append(static_cast<char>(crc & 0xFF));
        payload.append(static_cast<char>((crc >> 8) & 0xFF));
    }

    QByteArray result;
    result.reserve(payload.size() * 2 + 2);
    result.append(static_cast<char>(FLAG));

    for (int i = 0; i < payload.size(); i++) {
        uint8_t b = static_cast<uint8_t>(payload[i]);
        if (b == FLAG || b == ESCAPE) {
            result.append(static_cast<char>(ESCAPE));
            result.append(static_cast<char>(b ^ ESCAPE_XOR));
        } else {
            result.append(static_cast<char>(b));
        }
    }

    result.append(static_cast<char>(FLAG));
    return result;
}

QByteArray HdlcCodec::decode(const QByteArray& frame, bool validateCrc)
{
    // Strip leading and trailing flags
    int start = 0, end = frame.size() - 1;
    while (start < frame.size() && static_cast<uint8_t>(frame[start]) == FLAG) start++;
    while (end > start && static_cast<uint8_t>(frame[end]) == FLAG) end--;

    QByteArray unescaped = unescape(frame.mid(start, end - start + 1));

    if (validateCrc && unescaped.size() >= 2) {
        // Last 2 bytes are CRC
        QByteArray payload = unescaped.left(unescaped.size() - 2);
        uint16_t receivedCrc = static_cast<uint8_t>(unescaped[unescaped.size() - 2]) |
                               (static_cast<uint8_t>(unescaped[unescaped.size() - 1]) << 8);
        uint16_t computedCrc = Crc16::ccitt(reinterpret_cast<const uint8_t*>(payload.constData()), payload.size());

        if (receivedCrc != computedCrc)
            return {}; // CRC mismatch

        return payload;
    }

    return unescaped;
}

QByteArray HdlcCodec::escape(const QByteArray& data)
{
    QByteArray result;
    result.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); i++) {
        uint8_t b = static_cast<uint8_t>(data[i]);
        if (b == FLAG || b == ESCAPE) {
            result.append(static_cast<char>(ESCAPE));
            result.append(static_cast<char>(b ^ ESCAPE_XOR));
        } else {
            result.append(static_cast<char>(b));
        }
    }
    return result;
}

QByteArray HdlcCodec::unescape(const QByteArray& data)
{
    QByteArray result;
    result.reserve(data.size());
    bool escaped = false;
    for (int i = 0; i < data.size(); i++) {
        uint8_t b = static_cast<uint8_t>(data[i]);
        if (escaped) {
            result.append(static_cast<char>(b ^ ESCAPE_XOR));
            escaped = false;
        } else if (b == ESCAPE) {
            escaped = true;
        } else {
            result.append(static_cast<char>(b));
        }
    }
    return result;
}

QList<QByteArray> HdlcCodec::extractFrames(const QByteArray& data)
{
    QList<QByteArray> frames;
    int start = -1;
    for (int i = 0; i < data.size(); i++) {
        if (static_cast<uint8_t>(data[i]) == FLAG) {
            if (start >= 0 && i - start > 1) {
                frames.append(data.mid(start, i - start + 1));
            }
            start = i;
        }
    }
    return frames;
}

// --- Spreadtrum HDLC ---

QByteArray SprdHdlc::encode(uint16_t type, const QByteArray& payload, bool transcode)
{
    // Header: flag(1) + type(2) + length(2) + payload + checksum(2) + flag(1)
    uint16_t length = static_cast<uint16_t>(payload.size());
    QByteArray inner;
    inner.append(static_cast<char>((type >> 8) & 0xFF));  // Big-endian type
    inner.append(static_cast<char>(type & 0xFF));
    inner.append(static_cast<char>((length >> 8) & 0xFF)); // Big-endian length
    inner.append(static_cast<char>(length & 0xFF));
    inner.append(payload);

    // Checksum
    uint16_t sum = Crc16::sprdChecksum(reinterpret_cast<const uint8_t*>(inner.constData()), inner.size());
    inner.append(static_cast<char>((sum >> 8) & 0xFF));
    inner.append(static_cast<char>(sum & 0xFF));

    QByteArray result;
    result.append(static_cast<char>(0x7E));
    if (transcode) {
        result.append(HdlcCodec::escape(inner));
    } else {
        result.append(inner);
    }
    result.append(static_cast<char>(0x7E));

    return result;
}

SprdHdlc::Frame SprdHdlc::decode(const QByteArray& data, bool transcode)
{
    Frame frame;
    QByteArray content = data;

    // Strip flags
    if (!content.isEmpty() && static_cast<uint8_t>(content[0]) == 0x7E)
        content = content.mid(1);
    if (!content.isEmpty() && static_cast<uint8_t>(content.back()) == 0x7E)
        content.chop(1);

    if (transcode)
        content = HdlcCodec::unescape(content);

    if (content.size() < 6) return frame; // type(2)+length(2)+checksum(2) minimum

    frame.type = (static_cast<uint8_t>(content[0]) << 8) | static_cast<uint8_t>(content[1]);
    uint16_t length = (static_cast<uint8_t>(content[2]) << 8) | static_cast<uint8_t>(content[3]);

    if (4 + length + 2 > content.size()) return frame;

    frame.payload = content.mid(4, length);
    frame.valid = true;
    return frame;
}

} // namespace sakura
