#pragma once

#include <QByteArray>
#include <cstdint>

namespace sakura {

// Pure C++ LZ4 block and frame decoder
class Lz4Decoder {
public:
    // Decompress raw LZ4 block (requires known output size)
    static QByteArray decompressBlock(const QByteArray& compressed, int uncompressedSize);

    // Decompress LZ4 frame format (auto-detects size)
    static QByteArray decompressFrame(const QByteArray& data);

    // Try decompression, returns empty on failure
    static QByteArray tryDecompress(const QByteArray& data, int expectedSize = 0);

    // Check if data starts with LZ4 frame magic
    static bool isLz4Frame(const QByteArray& data);

    static constexpr uint32_t LZ4_FRAME_MAGIC = 0x184D2204;
    static constexpr uint32_t LZ4_SKIPPABLE_MAGIC = 0x184D2A50;
};

} // namespace sakura
