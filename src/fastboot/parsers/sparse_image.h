#pragma once

#include "common/sparse_stream.h"

#include <QByteArray>
#include <cstdint>
#include <vector>

namespace sakura {

// ---------------------------------------------------------------------------
// SparseImage – Fastboot-specific sparse image helpers
//
// Wraps the low-level SparseStream utilities and adds Fastboot-specific
// functionality such as re-sparsing an image into chunks that fit the
// device's max-download-size.
// ---------------------------------------------------------------------------

class SparseImage {
public:
    /// Check whether the data begins with the sparse magic.
    static bool isSparse(const QByteArray& data);

    /// Split a sparse image into transfer-sized chunks.
    ///
    /// Each returned QByteArray is a self-contained sparse image that can be
    /// individually downloaded and flashed.  The device will assemble the
    /// chunks in order.
    ///
    /// @param sparseData     The original sparse image.
    /// @param maxDownloadSize  Maximum per-download payload size (bytes).
    /// @return A vector of sparse image chunks.  If the input already fits in
    ///         a single download, a vector with one element is returned.
    static std::vector<QByteArray> splitForTransfer(const QByteArray& sparseData,
                                                     uint32_t maxDownloadSize);

    /// Re-sparse a raw image into transfer-sized sparse chunks.
    ///
    /// Useful when the caller has raw (non-sparse) data that must be sent via
    /// Fastboot sparse protocol because it exceeds max-download-size.
    static std::vector<QByteArray> rawToTransferChunks(const QByteArray& rawData,
                                                        uint32_t maxDownloadSize);

    /// Get the total raw (unsparsed) image size.
    static qint64 getRawSize(const QByteArray& sparseData);

private:
    /// Build a sparse image from selected chunk indices of the original.
    static QByteArray buildSparseFromChunks(const QByteArray& original,
                                            const SparseHeader& hdr,
                                            const std::vector<int>& chunkIndices,
                                            uint32_t totalBlocks);

    SparseImage() = delete;
};

} // namespace sakura
