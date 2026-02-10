#include "lz4_decoder.h"
#include <cstring>

namespace sakura {

bool Lz4Decoder::isLz4Frame(const QByteArray& data)
{
    if (data.size() < 4) return false;
    uint32_t magic;
    std::memcpy(&magic, data.constData(), 4);
    return magic == LZ4_FRAME_MAGIC;
}

QByteArray Lz4Decoder::decompressBlock(const QByteArray& compressed, int uncompressedSize)
{
    if (compressed.isEmpty() || uncompressedSize <= 0)
        return {};

    QByteArray output(uncompressedSize, '\0');
    const uint8_t* src = reinterpret_cast<const uint8_t*>(compressed.constData());
    uint8_t* dst = reinterpret_cast<uint8_t*>(output.data());
    int srcPos = 0, dstPos = 0;
    int srcSize = compressed.size();

    while (srcPos < srcSize && dstPos < uncompressedSize) {
        uint8_t token = src[srcPos++];
        int literalLen = (token >> 4) & 0x0F;

        // Read extended literal length
        if (literalLen == 15) {
            while (srcPos < srcSize) {
                uint8_t extra = src[srcPos++];
                literalLen += extra;
                if (extra != 255) break;
            }
        }

        // Copy literals
        if (literalLen > 0) {
            if (srcPos + literalLen > srcSize || dstPos + literalLen > uncompressedSize)
                break;
            std::memcpy(dst + dstPos, src + srcPos, literalLen);
            srcPos += literalLen;
            dstPos += literalLen;
        }

        if (dstPos >= uncompressedSize) break;

        // Read match offset (little-endian 16-bit)
        if (srcPos + 2 > srcSize) break;
        uint16_t offset = src[srcPos] | (src[srcPos + 1] << 8);
        srcPos += 2;

        if (offset == 0) break; // Invalid offset

        // Match length
        int matchLen = (token & 0x0F) + 4; // Minimum match = 4
        if ((token & 0x0F) == 15) {
            while (srcPos < srcSize) {
                uint8_t extra = src[srcPos++];
                matchLen += extra;
                if (extra != 255) break;
            }
        }

        // Copy match (may overlap)
        int matchPos = dstPos - offset;
        if (matchPos < 0) break;

        for (int i = 0; i < matchLen && dstPos < uncompressedSize; i++) {
            dst[dstPos++] = dst[matchPos + i];
        }
    }

    output.resize(dstPos);
    return output;
}

QByteArray Lz4Decoder::decompressFrame(const QByteArray& data)
{
    if (!isLz4Frame(data)) return {};

    int pos = 4; // Skip magic
    if (pos >= data.size()) return {};

    // Frame descriptor
    uint8_t flg = static_cast<uint8_t>(data[pos++]);
    if (pos >= data.size()) return {};
    uint8_t bd = static_cast<uint8_t>(data[pos++]);
    Q_UNUSED(bd);

    bool hasContentSize = (flg >> 3) & 1;
    bool hasChecksum = (flg >> 2) & 1;
    Q_UNUSED(hasChecksum);

    uint64_t contentSize = 0;
    if (hasContentSize) {
        if (pos + 8 > data.size()) return {};
        std::memcpy(&contentSize, data.constData() + pos, 8);
        pos += 8;
    }

    pos++; // Header checksum

    // Read data blocks
    QByteArray result;
    if (contentSize > 0) result.reserve(static_cast<int>(contentSize));

    while (pos + 4 <= data.size()) {
        uint32_t blockSize;
        std::memcpy(&blockSize, data.constData() + pos, 4);
        pos += 4;

        if (blockSize == 0) break; // End mark

        bool uncompressed = (blockSize >> 31) & 1;
        blockSize &= 0x7FFFFFFF;

        if (pos + static_cast<int>(blockSize) > data.size()) break;

        QByteArray blockData = data.mid(pos, blockSize);
        pos += blockSize;

        if (uncompressed) {
            result.append(blockData);
        } else {
            // Need to know output size - use content size or estimate
            int estSize = contentSize > 0 ? static_cast<int>(contentSize - result.size())
                                          : static_cast<int>(blockSize * 4);
            QByteArray decompressed = decompressBlock(blockData, estSize);
            result.append(decompressed);
        }
    }

    return result;
}

QByteArray Lz4Decoder::tryDecompress(const QByteArray& data, int expectedSize)
{
    if (isLz4Frame(data))
        return decompressFrame(data);
    if (expectedSize > 0)
        return decompressBlock(data, expectedSize);
    return {};
}

} // namespace sakura
