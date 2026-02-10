#include "sparse_image.h"
#include "core/logger.h"

#include <QDataStream>
#include <QtEndian>
#include <cstring>

namespace sakura {

static constexpr const char* TAG = "SparseImage";

// ---------------------------------------------------------------------------
// isSparse
// ---------------------------------------------------------------------------

bool SparseImage::isSparse(const QByteArray& data)
{
    return SparseStream::isSparse(data);
}

// ---------------------------------------------------------------------------
// getRawSize
// ---------------------------------------------------------------------------

qint64 SparseImage::getRawSize(const QByteArray& sparseData)
{
    return SparseStream::getRealSize(sparseData);
}

// ---------------------------------------------------------------------------
// splitForTransfer
// ---------------------------------------------------------------------------

std::vector<QByteArray> SparseImage::splitForTransfer(const QByteArray& sparseData,
                                                       uint32_t maxDownloadSize)
{
    std::vector<QByteArray> result;

    if (!isSparse(sparseData) || sparseData.size() < static_cast<int>(sizeof(SparseHeader))) {
        // Not sparse or too small – return as-is
        result.push_back(sparseData);
        return result;
    }

    // If it already fits, no splitting needed
    if (static_cast<uint32_t>(sparseData.size()) <= maxDownloadSize) {
        result.push_back(sparseData);
        return result;
    }

    // Parse the sparse header
    SparseHeader hdr;
    std::memcpy(&hdr, sparseData.constData(), sizeof(SparseHeader));

    // Walk chunks and assign them into buckets that fit maxDownloadSize
    int offset = hdr.fileHeaderSize;
    std::vector<int>      currentChunkIndices;
    std::vector<std::pair<int, int>> chunkOffsets; // (offset, total_size) per chunk
    uint32_t currentSize = sizeof(SparseHeader);
    uint32_t currentBlocks = 0;

    for (uint32_t i = 0; i < hdr.totalChunks; ++i) {
        if (offset + static_cast<int>(sizeof(SparseChunkHeader)) > sparseData.size())
            break;

        SparseChunkHeader chHdr;
        std::memcpy(&chHdr, sparseData.constData() + offset, sizeof(SparseChunkHeader));

        chunkOffsets.push_back({offset, static_cast<int>(chHdr.totalSize)});

        uint32_t chunkTotalSize = chHdr.totalSize;

        // Would adding this chunk exceed the limit?
        if (!currentChunkIndices.empty() &&
            (currentSize + chunkTotalSize) > maxDownloadSize) {
            // Flush current bucket
            result.push_back(buildSparseFromChunks(sparseData, hdr,
                                                    currentChunkIndices, currentBlocks));
            currentChunkIndices.clear();
            currentSize = sizeof(SparseHeader);
            currentBlocks = 0;
        }

        currentChunkIndices.push_back(static_cast<int>(i));
        currentSize   += chunkTotalSize;
        currentBlocks += chHdr.chunkBlocks;

        offset += static_cast<int>(chunkTotalSize);
    }

    // Flush remaining
    if (!currentChunkIndices.empty()) {
        result.push_back(buildSparseFromChunks(sparseData, hdr,
                                                currentChunkIndices, currentBlocks));
    }

    LOG_INFO_CAT(TAG, QStringLiteral("Split sparse image into %1 chunk(s)")
                          .arg(result.size()));
    return result;
}

// ---------------------------------------------------------------------------
// rawToTransferChunks
// ---------------------------------------------------------------------------

std::vector<QByteArray> SparseImage::rawToTransferChunks(const QByteArray& rawData,
                                                          uint32_t maxDownloadSize)
{
    // Convert raw data to sparse chunks via SparseStream, then split
    auto chunks = SparseStream::rawToSparseChunks(rawData, maxDownloadSize);

    std::vector<QByteArray> result;
    result.reserve(chunks.size());
    for (auto& c : chunks)
        result.push_back(std::move(c));
    return result;
}

// ---------------------------------------------------------------------------
// buildSparseFromChunks – assemble a new sparse image from selected chunks
// ---------------------------------------------------------------------------

QByteArray SparseImage::buildSparseFromChunks(const QByteArray& original,
                                               const SparseHeader& origHdr,
                                               const std::vector<int>& chunkIndices,
                                               uint32_t totalBlocks)
{
    // Walk original to locate chunk offsets
    std::vector<std::pair<int, int>> chunkLocations; // (offset, size)
    int offset = origHdr.fileHeaderSize;
    for (uint32_t i = 0; i < origHdr.totalChunks; ++i) {
        if (offset + static_cast<int>(sizeof(SparseChunkHeader)) > original.size())
            break;
        SparseChunkHeader chHdr;
        std::memcpy(&chHdr, original.constData() + offset, sizeof(SparseChunkHeader));
        chunkLocations.push_back({offset, static_cast<int>(chHdr.totalSize)});
        offset += static_cast<int>(chHdr.totalSize);
    }

    // Build new sparse image
    QByteArray out;
    out.reserve(static_cast<int>(sizeof(SparseHeader)) + 1024 * 1024);

    // Write header
    SparseHeader newHdr = origHdr;
    newHdr.totalChunks = static_cast<uint32_t>(chunkIndices.size());
    newHdr.totalBlocks = totalBlocks;
    newHdr.imageCrc32  = 0; // recalculation optional
    out.append(reinterpret_cast<const char*>(&newHdr), sizeof(SparseHeader));

    // Write selected chunks
    for (int idx : chunkIndices) {
        if (idx < 0 || idx >= static_cast<int>(chunkLocations.size()))
            continue;
        auto [chOff, chSize] = chunkLocations[static_cast<size_t>(idx)];
        out.append(original.mid(chOff, chSize));
    }

    return out;
}

} // namespace sakura
