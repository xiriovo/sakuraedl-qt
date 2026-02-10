#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QFile>
#include <cstdint>
#include <vector>

namespace sakura {

// Android Sparse image format constants
constexpr uint32_t SPARSE_HEADER_MAGIC = 0xED26FF3A;
constexpr uint16_t CHUNK_TYPE_RAW      = 0xCAC1;
constexpr uint16_t CHUNK_TYPE_FILL     = 0xCAC2;
constexpr uint16_t CHUNK_TYPE_DONT_CARE= 0xCAC3;
constexpr uint16_t CHUNK_TYPE_CRC32    = 0xCAC4;

#pragma pack(push, 1)
struct SparseHeader {
    uint32_t magic;
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint16_t fileHeaderSize;
    uint16_t chunkHeaderSize;
    uint32_t blockSize;
    uint32_t totalBlocks;
    uint32_t totalChunks;
    uint32_t imageCrc32;
};

struct SparseChunkHeader {
    uint16_t chunkType;
    uint16_t reserved;
    uint32_t chunkBlocks;
    uint32_t totalSize;  // chunk header + data size
};
#pragma pack(pop)

class SparseStream {
public:
    static bool isSparse(const QByteArray& data);
    static bool isSparseFile(const QString& path);
    static qint64 getRealSize(const QByteArray& sparseData);

    // Convert sparse image to raw
    static QByteArray toRaw(const QByteArray& sparseData);
    static bool convertToRaw(const QString& sparsePath, const QString& rawPath,
                              std::function<void(qint64, qint64)> progress = nullptr);

    // Split raw image into sparse chunks for transfer
    static std::vector<QByteArray> rawToSparseChunks(const QByteArray& rawData,
                                                       uint32_t maxChunkSize);

    // Read a specific range from sparse data as if it were raw
    static QByteArray readRange(const QByteArray& sparseData, qint64 offset, qint64 size);

private:
    struct ChunkInfo {
        uint16_t type;
        uint32_t blocks;
        qint64 dataOffset; // offset in sparse file
        qint64 rawOffset;  // offset in raw image
        qint64 rawSize;
        uint32_t fillValue;
    };

    static std::vector<ChunkInfo> parseChunks(const QByteArray& sparseData);
};

} // namespace sakura
