#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sakura {

// ---------------------------------------------------------------------------
// OTA payload.bin format structures
//
// Android's OTA payload.bin uses a protobuf-encoded manifest that describes
// how to reconstruct each partition.  This parser reads the header, manifest,
// and allows extraction of individual partitions.
//
// Wire layout:
//   [magic "CrAU"] [file_format_version: 8 bytes] [manifest_size: 8 bytes]
//   [metadata_signature_size: 4 bytes (v2+)] [manifest blob] [metadata sig]
//   [data blobs ...]
// ---------------------------------------------------------------------------

// --- Operation types -------------------------------------------------------

enum class PayloadOpType : uint32_t {
    Replace     = 0,   // Raw data replacement
    ReplaceBz   = 1,   // bzip2-compressed replacement
    Move        = 2,   // Block-level move (source → target)
    Bsdiff      = 3,   // Binary diff (source → target)
    SourceCopy  = 4,   // Copy from source partition
    SourceBsdiff= 5,   // Bsdiff from source partition
    ReplaceXz   = 8,   // xz-compressed replacement
    Zero        = 6,   // Fill with zeros
    Discard     = 7,   // Mark extents as unused
    Puffdiff    = 9,   // Puffdiff (deflate-aware)
    Brotli      = 10,  // Brotli-compressed replacement
    Zucchini    = 11,  // Zucchini diff
    LZ4diff     = 12,  // LZ4-diff
};

// --- Data extent -----------------------------------------------------------

struct PayloadExtent {
    uint64_t startBlock = 0;
    uint64_t numBlocks  = 0;
};

// --- Install operation -----------------------------------------------------

struct PayloadOperation {
    PayloadOpType              type       = PayloadOpType::Replace;
    uint64_t                   dataOffset = 0;
    uint64_t                   dataLength = 0;
    std::vector<PayloadExtent> srcExtents;
    std::vector<PayloadExtent> dstExtents;
    QByteArray                 dataHash;
    QByteArray                 srcHash;
};

// --- Partition description -------------------------------------------------

struct PayloadPartition {
    QString                        name;
    std::vector<PayloadOperation>  operations;
    uint64_t                       size = 0;       // new partition size in bytes
    QByteArray                     hash;           // expected hash
};

// ---------------------------------------------------------------------------
// PayloadParser
// ---------------------------------------------------------------------------

class PayloadParser {
public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    PayloadParser();
    ~PayloadParser();

    /// Load a payload.bin file.  Returns true if the header + manifest
    /// were successfully parsed.
    bool load(const QString& path);

    /// Whether a payload is currently loaded.
    bool isLoaded() const { return m_loaded; }

    /// Payload format version (typically 2).
    uint64_t formatVersion() const { return m_formatVersion; }

    /// Block size used by the payload (usually 4096).
    uint32_t blockSize() const { return m_blockSize; }

    /// List of partition names in the payload.
    QStringList partitionNames() const;

    /// Full partition descriptors.
    const std::vector<PayloadPartition>& partitions() const { return m_partitions; }

    /// Find a partition by name (nullptr if not found).
    const PayloadPartition* partition(const QString& name) const;

    /// Extract a single partition to a file.
    bool extractPartition(const QString& name, const QString& outPath,
                          ProgressCallback progress = nullptr);

private:
    bool parseHeader();
    bool parseManifest(const QByteArray& manifestData);

    /// Read raw operation data from the payload file.
    QByteArray readOperationData(uint64_t offset, uint64_t length);

    /// Decompress operation data according to the operation type.
    QByteArray decompressData(const QByteArray& compressed, PayloadOpType type);

    std::unique_ptr<QFile>          m_file;
    bool                            m_loaded        = false;
    uint64_t                        m_formatVersion = 0;
    uint64_t                        m_manifestSize  = 0;
    uint32_t                        m_metaSigSize   = 0;
    uint64_t                        m_dataOffset    = 0; // offset to first data blob
    uint32_t                        m_blockSize     = 4096;
    std::vector<PayloadPartition>   m_partitions;
};

} // namespace sakura
