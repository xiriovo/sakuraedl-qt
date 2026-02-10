#pragma once

#include <QByteArray>
#include <cstdint>

namespace sakura {

// LZMA/XZ decoder using liblzma (for firmware decompression)
class LzmaDecoder {
public:
    // Decompress LZMA1 stream (5-byte properties + 8-byte size + compressed data)
    static QByteArray decompress(const QByteArray& data);

    // Decompress XZ container format
    static QByteArray decompressXz(const QByteArray& data);

    // Check if data looks like LZMA compressed
    static bool isLzma(const QByteArray& data);

    // Check if data is XZ format (magic: FD 37 7A 58 5A 00)
    static bool isXz(const QByteArray& data);

    // Auto-detect and decompress (tries XZ, then LZMA1)
    static QByteArray autoDecompress(const QByteArray& data);
};

} // namespace sakura
