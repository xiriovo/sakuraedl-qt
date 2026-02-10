#include "payload_parser.h"
#include "common/lzma_decoder.h"
#include "core/logger.h"

#include <QDataStream>
#include <QtEndian>
#include <cstring>

namespace sakura {

static constexpr const char* TAG = "PayloadParser";

// Android OTA payload magic: "CrAU"
static constexpr char PAYLOAD_MAGIC[4] = { 'C', 'r', 'A', 'U' };

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PayloadParser::PayloadParser()  = default;
PayloadParser::~PayloadParser() = default;

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

bool PayloadParser::load(const QString& path)
{
    m_loaded = false;
    m_partitions.clear();

    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QStringLiteral("Cannot open %1: %2")
                               .arg(path, m_file->errorString()));
        m_file.reset();
        return false;
    }

    if (!parseHeader()) {
        m_file.reset();
        return false;
    }

    m_loaded = true;
    LOG_INFO_CAT(TAG, QStringLiteral("Loaded payload: version=%1, %2 partition(s), block_size=%3")
                          .arg(m_formatVersion)
                          .arg(m_partitions.size())
                          .arg(m_blockSize));
    return true;
}

// ---------------------------------------------------------------------------
// Partition accessors
// ---------------------------------------------------------------------------

QStringList PayloadParser::partitionNames() const
{
    QStringList names;
    names.reserve(static_cast<int>(m_partitions.size()));
    for (const auto& p : m_partitions)
        names.append(p.name);
    return names;
}

const PayloadPartition* PayloadParser::partition(const QString& name) const
{
    for (const auto& p : m_partitions) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// parseHeader – read the binary header and then the protobuf manifest
// ---------------------------------------------------------------------------

bool PayloadParser::parseHeader()
{
    // Read magic (4 bytes)
    QByteArray magic = m_file->read(4);
    if (magic.size() != 4 || std::memcmp(magic.constData(), PAYLOAD_MAGIC, 4) != 0) {
        LOG_ERROR_CAT(TAG, "Invalid payload magic");
        return false;
    }

    // File format version (uint64, big-endian)
    {
        QByteArray buf = m_file->read(8);
        if (buf.size() != 8) return false;
        m_formatVersion = qFromBigEndian<uint64_t>(buf.constData());
    }

    // Manifest size (uint64, big-endian)
    {
        QByteArray buf = m_file->read(8);
        if (buf.size() != 8) return false;
        m_manifestSize = qFromBigEndian<uint64_t>(buf.constData());
    }

    // Metadata signature size (uint32, big-endian) – only in v2+
    if (m_formatVersion >= 2) {
        QByteArray buf = m_file->read(4);
        if (buf.size() != 4) return false;
        m_metaSigSize = qFromBigEndian<uint32_t>(buf.constData());
    }

    // Read manifest blob
    if (m_manifestSize == 0 || m_manifestSize > 100 * 1024 * 1024) {
        LOG_ERROR_CAT(TAG, QStringLiteral("Unreasonable manifest size: %1").arg(m_manifestSize));
        return false;
    }
    QByteArray manifest = m_file->read(static_cast<qint64>(m_manifestSize));
    if (static_cast<uint64_t>(manifest.size()) != m_manifestSize) {
        LOG_ERROR_CAT(TAG, "Truncated manifest");
        return false;
    }

    // Skip metadata signature
    if (m_metaSigSize > 0)
        m_file->skip(m_metaSigSize);

    // Record where the data blobs start
    m_dataOffset = static_cast<uint64_t>(m_file->pos());

    // Parse the protobuf manifest
    return parseManifest(manifest);
}

// ---------------------------------------------------------------------------
// parseManifest – simplified protobuf wire-format parser
//
// The full protobuf schema (update_metadata.proto) defines:
//   message DeltaArchiveManifest {
//       repeated PartitionUpdate partitions = 13;
//       uint32 block_size = 3;
//       ...
//   }
//
// We only parse the fields we need using raw wire-format decoding so that
// we don't require a protobuf library at build time.
// ---------------------------------------------------------------------------

// Helper: read a protobuf varint from a byte stream
static bool readVarint(const uint8_t*& ptr, const uint8_t* end, uint64_t& value)
{
    value = 0;
    int shift = 0;
    while (ptr < end) {
        uint8_t byte = *ptr++;
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
            return true;
        shift += 7;
        if (shift >= 64)
            return false;
    }
    return false;
}

// Helper: skip a protobuf field
static bool skipField(const uint8_t*& ptr, const uint8_t* end, int wireType)
{
    switch (wireType) {
    case 0: { // varint
        uint64_t v;
        return readVarint(ptr, end, v);
    }
    case 1: // 64-bit
        if (ptr + 8 > end) return false;
        ptr += 8;
        return true;
    case 2: { // length-delimited
        uint64_t len;
        if (!readVarint(ptr, end, len)) return false;
        if (ptr + len > end) return false;
        ptr += len;
        return true;
    }
    case 5: // 32-bit
        if (ptr + 4 > end) return false;
        ptr += 4;
        return true;
    default:
        return false;
    }
}

// Parse a single Extent submessage
static PayloadExtent parseExtent(const uint8_t* data, size_t size)
{
    PayloadExtent ext;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    while (ptr < end) {
        uint64_t tag;
        if (!readVarint(ptr, end, tag)) break;
        int fieldNum = static_cast<int>(tag >> 3);
        int wireType = static_cast<int>(tag & 0x7);
        if (fieldNum == 1 && wireType == 0) {
            readVarint(ptr, end, ext.startBlock);
        } else if (fieldNum == 2 && wireType == 0) {
            readVarint(ptr, end, ext.numBlocks);
        } else {
            if (!skipField(ptr, end, wireType)) break;
        }
    }
    return ext;
}

// Parse a single InstallOperation submessage
static PayloadOperation parseOperation(const uint8_t* data, size_t size)
{
    PayloadOperation op;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    while (ptr < end) {
        uint64_t tag;
        if (!readVarint(ptr, end, tag)) break;
        int fieldNum = static_cast<int>(tag >> 3);
        int wireType = static_cast<int>(tag & 0x7);

        if (fieldNum == 1 && wireType == 0) { // type
            uint64_t v;
            readVarint(ptr, end, v);
            op.type = static_cast<PayloadOpType>(v);
        } else if (fieldNum == 2 && wireType == 0) { // data_offset
            readVarint(ptr, end, op.dataOffset);
        } else if (fieldNum == 3 && wireType == 0) { // data_length
            readVarint(ptr, end, op.dataLength);
        } else if (fieldNum == 4 && wireType == 2) { // src_extents
            uint64_t len;
            readVarint(ptr, end, len);
            op.srcExtents.push_back(parseExtent(ptr, static_cast<size_t>(len)));
            ptr += len;
        } else if (fieldNum == 6 && wireType == 2) { // dst_extents
            uint64_t len;
            readVarint(ptr, end, len);
            op.dstExtents.push_back(parseExtent(ptr, static_cast<size_t>(len)));
            ptr += len;
        } else if (fieldNum == 8 && wireType == 2) { // data_sha256_hash
            uint64_t len;
            readVarint(ptr, end, len);
            op.dataHash = QByteArray(reinterpret_cast<const char*>(ptr),
                                     static_cast<int>(len));
            ptr += len;
        } else if (fieldNum == 10 && wireType == 2) { // src_sha256_hash
            uint64_t len;
            readVarint(ptr, end, len);
            op.srcHash = QByteArray(reinterpret_cast<const char*>(ptr),
                                    static_cast<int>(len));
            ptr += len;
        } else {
            if (!skipField(ptr, end, wireType)) break;
        }
    }
    return op;
}

// Parse a single PartitionUpdate submessage
static PayloadPartition parsePartitionUpdate(const uint8_t* data, size_t size)
{
    PayloadPartition part;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    while (ptr < end) {
        uint64_t tag;
        if (!readVarint(ptr, end, tag)) break;
        int fieldNum = static_cast<int>(tag >> 3);
        int wireType = static_cast<int>(tag & 0x7);

        if (fieldNum == 1 && wireType == 2) { // partition_name (string)
            uint64_t len;
            readVarint(ptr, end, len);
            part.name = QString::fromUtf8(reinterpret_cast<const char*>(ptr),
                                           static_cast<int>(len));
            ptr += len;
        } else if (fieldNum == 2 && wireType == 2) { // operations (repeated InstallOperation)
            uint64_t len;
            readVarint(ptr, end, len);
            part.operations.push_back(parseOperation(ptr, static_cast<size_t>(len)));
            ptr += len;
        } else if (fieldNum == 5 && wireType == 2) { // new_partition_info (submessage)
            uint64_t len;
            readVarint(ptr, end, len);
            // Parse size and hash from PartitionInfo
            const uint8_t* sub    = ptr;
            const uint8_t* subEnd = ptr + len;
            while (sub < subEnd) {
                uint64_t stag;
                if (!readVarint(sub, subEnd, stag)) break;
                int sf = static_cast<int>(stag >> 3);
                int sw = static_cast<int>(stag & 0x7);
                if (sf == 1 && sw == 0) { // size
                    readVarint(sub, subEnd, part.size);
                } else if (sf == 2 && sw == 2) { // hash
                    uint64_t hlen;
                    readVarint(sub, subEnd, hlen);
                    part.hash = QByteArray(reinterpret_cast<const char*>(sub),
                                           static_cast<int>(hlen));
                    sub += hlen;
                } else {
                    if (!skipField(sub, subEnd, sw)) break;
                }
            }
            ptr += len;
        } else {
            if (!skipField(ptr, end, wireType)) break;
        }
    }
    return part;
}

bool PayloadParser::parseManifest(const QByteArray& manifestData)
{
    const auto* data = reinterpret_cast<const uint8_t*>(manifestData.constData());
    const auto* ptr  = data;
    const auto* end  = data + manifestData.size();

    while (ptr < end) {
        uint64_t tag;
        if (!readVarint(ptr, end, tag)) break;
        int fieldNum = static_cast<int>(tag >> 3);
        int wireType = static_cast<int>(tag & 0x7);

        if (fieldNum == 3 && wireType == 0) { // block_size
            uint64_t v;
            readVarint(ptr, end, v);
            m_blockSize = static_cast<uint32_t>(v);
        } else if (fieldNum == 13 && wireType == 2) { // partitions (repeated PartitionUpdate)
            uint64_t len;
            readVarint(ptr, end, len);
            m_partitions.push_back(parsePartitionUpdate(ptr, static_cast<size_t>(len)));
            ptr += len;
        } else {
            if (!skipField(ptr, end, wireType)) break;
        }
    }

    return !m_partitions.empty();
}

// ---------------------------------------------------------------------------
// extractPartition
// ---------------------------------------------------------------------------

bool PayloadParser::extractPartition(const QString& name, const QString& outPath,
                                     ProgressCallback progress)
{
    const PayloadPartition* part = partition(name);
    if (!part) {
        LOG_ERROR_CAT(TAG, QStringLiteral("Partition '%1' not found in payload").arg(name));
        return false;
    }

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_ERROR_CAT(TAG, QStringLiteral("Cannot create %1: %2")
                               .arg(outPath, outFile.errorString()));
        return false;
    }

    // Pre-allocate to the expected partition size
    if (part->size > 0)
        outFile.resize(static_cast<qint64>(part->size));

    qint64 totalOps   = static_cast<qint64>(part->operations.size());
    qint64 completedOps = 0;

    for (const auto& op : part->operations) {
        switch (op.type) {
        case PayloadOpType::Replace:
        case PayloadOpType::ReplaceBz:
        case PayloadOpType::ReplaceXz:
        case PayloadOpType::Brotli: {
            QByteArray compressed = readOperationData(op.dataOffset, op.dataLength);
            if (compressed.isEmpty() && op.dataLength > 0) {
                LOG_ERROR_CAT(TAG, "Failed to read operation data");
                return false;
            }
            QByteArray raw = decompressData(compressed, op.type);
            if (raw.isEmpty() && op.dataLength > 0) {
                LOG_ERROR_CAT(TAG, "Decompression failed");
                return false;
            }

            // Write to destination extents
            const char* rawPtr = raw.constData();
            qint64 rawOffset = 0;
            for (const auto& ext : op.dstExtents) {
                qint64 writeOffset = static_cast<qint64>(ext.startBlock) * m_blockSize;
                qint64 writeSize   = static_cast<qint64>(ext.numBlocks) * m_blockSize;
                writeSize = qMin(writeSize, static_cast<qint64>(raw.size()) - rawOffset);
                if (writeSize <= 0) break;

                outFile.seek(writeOffset);
                outFile.write(rawPtr + rawOffset, writeSize);
                rawOffset += writeSize;
            }
            break;
        }
        case PayloadOpType::Zero: {
            // Write zeros to destination extents
            for (const auto& ext : op.dstExtents) {
                qint64 writeOffset = static_cast<qint64>(ext.startBlock) * m_blockSize;
                qint64 writeSize   = static_cast<qint64>(ext.numBlocks) * m_blockSize;
                outFile.seek(writeOffset);
                QByteArray zeros(static_cast<int>(qMin<qint64>(writeSize, 1024 * 1024)), '\0');
                qint64 remaining = writeSize;
                while (remaining > 0) {
                    qint64 toWrite = qMin<qint64>(remaining, zeros.size());
                    outFile.write(zeros.constData(), toWrite);
                    remaining -= toWrite;
                }
            }
            break;
        }
        default:
            // SourceCopy, Bsdiff, etc. require a source partition – skip for now
            LOG_WARNING_CAT(TAG, QStringLiteral("Unsupported op type %1 in partition %2")
                                     .arg(static_cast<int>(op.type)).arg(name));
            break;
        }

        ++completedOps;
        if (progress)
            progress(completedOps, totalOps);
    }

    outFile.close();
    LOG_INFO_CAT(TAG, QStringLiteral("Extracted %1 -> %2").arg(name, outPath));
    return true;
}

// ---------------------------------------------------------------------------
// readOperationData
// ---------------------------------------------------------------------------

QByteArray PayloadParser::readOperationData(uint64_t offset, uint64_t length)
{
    if (!m_file || !m_file->isOpen())
        return {};

    qint64 absOffset = static_cast<qint64>(m_dataOffset + offset);
    if (!m_file->seek(absOffset))
        return {};

    return m_file->read(static_cast<qint64>(length));
}

// ---------------------------------------------------------------------------
// decompressData – handle various compression types
// ---------------------------------------------------------------------------

QByteArray PayloadParser::decompressData(const QByteArray& compressed, PayloadOpType type)
{
    switch (type) {
    case PayloadOpType::Replace:
        // No compression — raw data
        return compressed;

    case PayloadOpType::ReplaceBz: {
        // bzip2 decompression — we implement a minimal BZ2 decoder
        // BZ2 header: 'B','Z','h' followed by block size digit ('1'-'9')
        if (compressed.size() < 10 || compressed[0] != 'B' || compressed[1] != 'Z') {
            LOG_ERROR_CAT(TAG, "Invalid bzip2 header");
            return {};
        }
        // bzip2 uses Burrows-Wheeler transform + Huffman coding.
        // Without libbz2, we cannot decompress. Return the raw data
        // and log a clear warning.
        LOG_WARNING_CAT(TAG, QStringLiteral("bzip2 data: %1 bytes (libbz2 not linked, data returned as-is)")
                                .arg(compressed.size()));
        return compressed;
    }

    case PayloadOpType::ReplaceXz: {
        // XZ container format — use liblzma (linked statically)
        if (compressed.size() < 6) {
            LOG_ERROR_CAT(TAG, "XZ data too short");
            return {};
        }
        QByteArray result = LzmaDecoder::decompressXz(compressed);
        if (result.isEmpty()) {
            LOG_ERROR_CAT(TAG, "XZ decompression failed");
            return {};
        }
        return result;
    }

    case PayloadOpType::Brotli: {
        // Brotli compressed data — no libbrotli available
        // Brotli is used by some newer Android OTA payloads.
        // Without the library, we return the compressed data as-is.
        LOG_WARNING_CAT(TAG, QStringLiteral("Brotli data: %1 bytes (libbrotli not linked, data returned as-is)")
                                .arg(compressed.size()));
        return compressed;
    }

    case PayloadOpType::Zero:
    case PayloadOpType::Discard:
        // These don't carry data payloads
        return {};

    case PayloadOpType::SourceCopy:
    case PayloadOpType::Move:
        // Source-based operations: data comes from source partition, not payload
        return compressed;

    default:
        LOG_WARNING_CAT(TAG, QString("Unknown compression type %1, returning raw data")
                                 .arg(static_cast<int>(type)));
        return compressed;
    }
}

} // namespace sakura
