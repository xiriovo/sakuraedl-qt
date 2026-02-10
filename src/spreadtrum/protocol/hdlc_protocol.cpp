#include "hdlc_protocol.h"
#include "common/crc_utils.h"

#include <QtEndian>

namespace sakura {

QByteArray SprdHdlcProtocol::encode(uint16_t type, const QByteArray& payload,
                                     bool transcode)
{
    QByteArray rawPacket = buildRawPacket(type, payload);

    if (!transcode) {
        // No HDLC escaping — just add flags
        QByteArray frame;
        frame.reserve(rawPacket.size() + 2);
        frame.append(static_cast<char>(HDLC_FLAG));
        frame.append(rawPacket);
        frame.append(static_cast<char>(HDLC_FLAG));
        return frame;
    }

    // Full HDLC encoding via SprdHdlc
    return SprdHdlc::encode(type, payload, transcode);
}

QByteArray SprdHdlcProtocol::decode(const QByteArray& data, bool transcode)
{
    if (data.isEmpty())
        return {};

    if (!transcode) {
        // No HDLC escaping — strip flags and extract
        QByteArray stripped = data;

        // Remove leading/trailing 0x7E flags
        while (!stripped.isEmpty() && static_cast<uint8_t>(stripped[0]) == HDLC_FLAG)
            stripped = stripped.mid(1);
        while (!stripped.isEmpty() &&
               static_cast<uint8_t>(stripped[stripped.size() - 1]) == HDLC_FLAG)
            stripped.chop(1);

        // Validate checksum if packet is large enough
        if (stripped.size() >= HEADER_SIZE + CHECKSUM_SIZE) {
            if (!validateChecksum(stripped)) {
                // Still return data — some devices omit checksum in non-transcode mode
            }
        }

        return stripped;
    }

    // Full HDLC decoding via SprdHdlc
    SprdHdlc::Frame frame = SprdHdlc::decode(data, transcode);
    if (!frame.valid)
        return {};

    // Reconstruct type + payload format
    QByteArray result;
    uint16_t beType = qToBigEndian(frame.type);
    result.append(reinterpret_cast<const char*>(&beType), 2);
    result.append(frame.payload);
    return result;
}

uint16_t SprdHdlcProtocol::extractType(const QByteArray& decodedPacket)
{
    if (decodedPacket.size() < 2)
        return 0;

    return qFromBigEndian<uint16_t>(
        reinterpret_cast<const uchar*>(decodedPacket.constData()));
}

QByteArray SprdHdlcProtocol::extractPayload(const QByteArray& decodedPacket)
{
    // Layout: type(2) + length(2) + payload(N) + checksum(2)
    if (decodedPacket.size() <= HEADER_SIZE + CHECKSUM_SIZE)
        return {};

    return decodedPacket.mid(HEADER_SIZE,
                             decodedPacket.size() - HEADER_SIZE - CHECKSUM_SIZE);
}

QByteArray SprdHdlcProtocol::buildRawPacket(uint16_t type, const QByteArray& payload)
{
    QByteArray packet;
    packet.reserve(HEADER_SIZE + payload.size() + CHECKSUM_SIZE);

    // Type (big-endian)
    uint16_t beType = qToBigEndian(type);
    packet.append(reinterpret_cast<const char*>(&beType), 2);

    // Length (big-endian) — payload length only
    uint16_t beLen = qToBigEndian(static_cast<uint16_t>(payload.size()));
    packet.append(reinterpret_cast<const char*>(&beLen), 2);

    // Payload
    packet.append(payload);

    // Checksum (over type + length + payload)
    uint16_t cksum = computeChecksum(packet);
    uint16_t beCksum = qToBigEndian(cksum);
    packet.append(reinterpret_cast<const char*>(&beCksum), 2);

    return packet;
}

bool SprdHdlcProtocol::validateChecksum(const QByteArray& rawPacket)
{
    if (rawPacket.size() < HEADER_SIZE + CHECKSUM_SIZE)
        return false;

    QByteArray dataToCheck = rawPacket.left(rawPacket.size() - CHECKSUM_SIZE);
    uint16_t computed = computeChecksum(dataToCheck);

    uint16_t stored = qFromBigEndian<uint16_t>(
        reinterpret_cast<const uchar*>(rawPacket.constData() + rawPacket.size() - 2));

    return computed == stored;
}

uint16_t SprdHdlcProtocol::computeChecksum(const QByteArray& data)
{
    // Spreadtrum uses a simple 16-bit sum checksum
    return Crc16::sprdChecksum(data);
}

} // namespace sakura
