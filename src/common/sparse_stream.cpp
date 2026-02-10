#include "sparse_stream.h"
#include "core/logger.h"
#include <cstring>

namespace sakura {

bool SparseStream::isSparse(const QByteArray& data)
{
    if (data.size() < static_cast<int>(sizeof(SparseHeader))) return false;
    uint32_t magic;
    std::memcpy(&magic, data.constData(), 4);
    return magic == SPARSE_HEADER_MAGIC;
}

bool SparseStream::isSparseFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray header = f.read(4);
    f.close();
    if (header.size() < 4) return false;
    uint32_t magic;
    std::memcpy(&magic, header.constData(), 4);
    return magic == SPARSE_HEADER_MAGIC;
}

qint64 SparseStream::getRealSize(const QByteArray& sparseData)
{
    if (!isSparse(sparseData)) return sparseData.size();
    SparseHeader hdr;
    std::memcpy(&hdr, sparseData.constData(), sizeof(SparseHeader));
    return static_cast<qint64>(hdr.totalBlocks) * hdr.blockSize;
}

std::vector<SparseStream::ChunkInfo> SparseStream::parseChunks(const QByteArray& sparseData)
{
    std::vector<ChunkInfo> chunks;
    if (!isSparse(sparseData)) return chunks;

    SparseHeader hdr;
    std::memcpy(&hdr, sparseData.constData(), sizeof(SparseHeader));

    qint64 offset = hdr.fileHeaderSize;
    qint64 rawOffset = 0;

    for (uint32_t i = 0; i < hdr.totalChunks; i++) {
        if (offset + sizeof(SparseChunkHeader) > sparseData.size()) break;

        SparseChunkHeader chunkHdr;
        std::memcpy(&chunkHdr, sparseData.constData() + offset, sizeof(SparseChunkHeader));

        ChunkInfo info;
        info.type = chunkHdr.chunkType;
        info.blocks = chunkHdr.chunkBlocks;
        info.rawOffset = rawOffset;
        info.rawSize = static_cast<qint64>(chunkHdr.chunkBlocks) * hdr.blockSize;
        info.dataOffset = offset + sizeof(SparseChunkHeader);
        info.fillValue = 0;

        if (chunkHdr.chunkType == CHUNK_TYPE_FILL && info.dataOffset + 4 <= sparseData.size()) {
            std::memcpy(&info.fillValue, sparseData.constData() + info.dataOffset, 4);
        }

        chunks.push_back(info);
        rawOffset += info.rawSize;
        offset += chunkHdr.totalSize;
    }
    return chunks;
}

QByteArray SparseStream::toRaw(const QByteArray& sparseData)
{
    if (!isSparse(sparseData)) return sparseData;

    SparseHeader hdr;
    std::memcpy(&hdr, sparseData.constData(), sizeof(SparseHeader));
    qint64 rawSize = static_cast<qint64>(hdr.totalBlocks) * hdr.blockSize;

    QByteArray raw(rawSize, '\0');
    auto chunks = parseChunks(sparseData);

    for (const auto& chunk : chunks) {
        switch (chunk.type) {
        case CHUNK_TYPE_RAW:
            if (chunk.dataOffset + chunk.rawSize <= sparseData.size()) {
                std::memcpy(raw.data() + chunk.rawOffset,
                            sparseData.constData() + chunk.dataOffset,
                            chunk.rawSize);
            }
            break;
        case CHUNK_TYPE_FILL: {
            uint32_t* dst = reinterpret_cast<uint32_t*>(raw.data() + chunk.rawOffset);
            qint64 count = chunk.rawSize / 4;
            for (qint64 j = 0; j < count; j++)
                dst[j] = chunk.fillValue;
            break;
        }
        case CHUNK_TYPE_DONT_CARE:
            // Already zeroed
            break;
        case CHUNK_TYPE_CRC32:
            // Skip CRC chunks
            break;
        }
    }

    return raw;
}

bool SparseStream::convertToRaw(const QString& sparsePath, const QString& rawPath,
                                  std::function<void(qint64, qint64)> progress)
{
    QFile sparseFile(sparsePath);
    if (!sparseFile.open(QIODevice::ReadOnly)) return false;

    QByteArray sparseData = sparseFile.readAll();
    sparseFile.close();

    if (!isSparse(sparseData)) {
        // Not sparse, just copy
        QFile::copy(sparsePath, rawPath);
        return true;
    }

    QByteArray raw = toRaw(sparseData);
    QFile outFile(rawPath);
    if (!outFile.open(QIODevice::WriteOnly)) return false;

    qint64 written = 0;
    constexpr qint64 chunkSize = 4 * 1024 * 1024;
    while (written < raw.size()) {
        qint64 toWrite = qMin(chunkSize, raw.size() - written);
        outFile.write(raw.constData() + written, toWrite);
        written += toWrite;
        if (progress) progress(written, raw.size());
    }

    outFile.close();
    return true;
}

QByteArray SparseStream::readRange(const QByteArray& sparseData, qint64 offset, qint64 size)
{
    if (!isSparse(sparseData))
        return sparseData.mid(offset, size);

    QByteArray result(size, '\0');
    auto chunks = parseChunks(sparseData);

    SparseHeader hdr;
    std::memcpy(&hdr, sparseData.constData(), sizeof(SparseHeader));

    for (const auto& chunk : chunks) {
        qint64 chunkEnd = chunk.rawOffset + chunk.rawSize;
        if (chunk.rawOffset >= offset + size || chunkEnd <= offset)
            continue;

        qint64 srcStart = qMax(offset, chunk.rawOffset);
        qint64 srcEnd = qMin(offset + size, chunkEnd);
        qint64 copyLen = srcEnd - srcStart;

        if (chunk.type == CHUNK_TYPE_RAW) {
            qint64 dataOff = chunk.dataOffset + (srcStart - chunk.rawOffset);
            if (dataOff + copyLen <= sparseData.size())
                std::memcpy(result.data() + (srcStart - offset),
                            sparseData.constData() + dataOff, copyLen);
        } else if (chunk.type == CHUNK_TYPE_FILL) {
            uint32_t* dst = reinterpret_cast<uint32_t*>(result.data() + (srcStart - offset));
            for (qint64 i = 0; i < copyLen / 4; i++)
                dst[i] = chunk.fillValue;
        }
        // DONT_CARE: already zeroed
    }
    return result;
}

std::vector<QByteArray> SparseStream::rawToSparseChunks(const QByteArray& rawData, uint32_t maxChunkSize)
{
    // Split a raw image into multiple sparse images, each small enough
    // to fit within the device's max-download-size for Fastboot transfer.
    //
    // Each chunk is a valid sparse image containing a subset of the blocks.

    constexpr uint32_t blockSize = 4096;
    const uint32_t totalBlocks = static_cast<uint32_t>((rawData.size() + blockSize - 1) / blockSize);

    // Maximum blocks per chunk: leave room for sparse header + chunk header
    const uint32_t overhead = sizeof(SparseHeader) + sizeof(SparseChunkHeader);
    const uint32_t maxDataPerChunk = maxChunkSize - overhead;
    const uint32_t blocksPerChunk = maxDataPerChunk / blockSize;

    if(blocksPerChunk == 0 || totalBlocks == 0)
        return {rawData};

    std::vector<QByteArray> result;
    uint32_t blocksDone = 0;

    while(blocksDone < totalBlocks) {
        uint32_t chunkBlocks = qMin(blocksPerChunk, totalBlocks - blocksDone);
        qint64 dataOffset = static_cast<qint64>(blocksDone) * blockSize;
        qint64 dataLen = qMin(static_cast<qint64>(chunkBlocks) * blockSize,
                              static_cast<qint64>(rawData.size()) - dataOffset);

        // Build sparse image header
        SparseHeader hdr{};
        hdr.magic = SPARSE_HEADER_MAGIC;
        hdr.majorVersion = 1;
        hdr.minorVersion = 0;
        hdr.fileHeaderSize = sizeof(SparseHeader);
        hdr.chunkHeaderSize = sizeof(SparseChunkHeader);
        hdr.blockSize = blockSize;
        hdr.totalBlocks = totalBlocks; // Original image total blocks
        hdr.totalChunks = 1;
        hdr.imageCrc32 = 0;

        // Chunk header for RAW data
        SparseChunkHeader chdr{};
        chdr.chunkType = CHUNK_TYPE_RAW;
        chdr.reserved = 0;
        chdr.chunkBlocks = chunkBlocks;
        chdr.totalSize = sizeof(SparseChunkHeader) + static_cast<uint32_t>(dataLen);

        QByteArray sparseChunk;
        sparseChunk.reserve(sizeof(SparseHeader) + sizeof(SparseChunkHeader) + dataLen);
        sparseChunk.append(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        sparseChunk.append(reinterpret_cast<const char*>(&chdr), sizeof(chdr));
        sparseChunk.append(rawData.mid(static_cast<int>(dataOffset), static_cast<int>(dataLen)));

        result.push_back(std::move(sparseChunk));
        blocksDone += chunkBlocks;
    }

    return result;
}

} // namespace sakura
