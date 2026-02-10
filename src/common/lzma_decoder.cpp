#include "lzma_decoder.h"
#include "core/logger.h"

#include <lzma.h>
#include <cstring>

namespace sakura {

static constexpr char LOG_TAG[] = "LZMA";

bool LzmaDecoder::isLzma(const QByteArray& data)
{
    if (data.size() < 13) return false;
    // LZMA1 header: properties byte (lc/lp/pb encoded) + dict size(4) + uncompressed size(8)
    uint8_t props = static_cast<uint8_t>(data[0]);
    int pb = (props / 9) / 5;
    return pb <= 4;
}

bool LzmaDecoder::isXz(const QByteArray& data)
{
    // XZ magic: FD 37 7A 58 5A 00
    if (data.size() < 6) return false;
    static const uint8_t XZ_MAGIC[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
    return std::memcmp(data.constData(), XZ_MAGIC, 6) == 0;
}

QByteArray LzmaDecoder::decompress(const QByteArray& data)
{
    if (data.size() < 13) return {};

    // Parse LZMA1 header to get uncompressed size
    uint64_t uncompSize;
    std::memcpy(&uncompSize, data.constData() + 5, 8);

    // Use liblzma's raw LZMA1 decoder
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_alone_decoder(&strm, UINT64_MAX);
    if (ret != LZMA_OK) {
        LOG_ERROR_CAT(LOG_TAG, QString("lzma_alone_decoder init failed: %1").arg(ret));
        return {};
    }

    // Input
    strm.next_in = reinterpret_cast<const uint8_t*>(data.constData());
    strm.avail_in = static_cast<size_t>(data.size());

    // Output - estimate from header or use 4x input size
    size_t outSize = (uncompSize != UINT64_MAX && uncompSize < 256ULL * 1024 * 1024)
                       ? static_cast<size_t>(uncompSize)
                       : static_cast<size_t>(data.size()) * 4;
    QByteArray output(static_cast<int>(outSize), 0);
    strm.next_out = reinterpret_cast<uint8_t*>(output.data());
    strm.avail_out = outSize;

    ret = lzma_code(&strm, LZMA_FINISH);

    if (ret == LZMA_STREAM_END || ret == LZMA_OK) {
        output.resize(static_cast<int>(strm.total_out));
        lzma_end(&strm);
        LOG_INFO_CAT(LOG_TAG, QString("LZMA decompressed: %1 → %2 bytes")
                                  .arg(data.size()).arg(output.size()));
        return output;
    }

    // If buffer was too small, retry with larger buffer
    if (ret == LZMA_BUF_ERROR && strm.total_out > 0) {
        outSize = static_cast<size_t>(strm.total_out) * 2;
        lzma_end(&strm);

        ret = lzma_alone_decoder(&strm, UINT64_MAX);
        if (ret != LZMA_OK) return {};

        strm.next_in = reinterpret_cast<const uint8_t*>(data.constData());
        strm.avail_in = static_cast<size_t>(data.size());
        output.resize(static_cast<int>(outSize));
        strm.next_out = reinterpret_cast<uint8_t*>(output.data());
        strm.avail_out = outSize;

        ret = lzma_code(&strm, LZMA_FINISH);
        output.resize(static_cast<int>(strm.total_out));
        lzma_end(&strm);

        if (ret == LZMA_STREAM_END || ret == LZMA_OK) {
            return output;
        }
    }

    lzma_end(&strm);
    LOG_ERROR_CAT(LOG_TAG, QString("LZMA decompression failed: %1").arg(ret));
    return {};
}

QByteArray LzmaDecoder::decompressXz(const QByteArray& data)
{
    if (data.size() < 6) return {};

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        LOG_ERROR_CAT(LOG_TAG, QString("lzma_stream_decoder init failed: %1").arg(ret));
        return {};
    }

    strm.next_in = reinterpret_cast<const uint8_t*>(data.constData());
    strm.avail_in = static_cast<size_t>(data.size());

    QByteArray output;
    output.reserve(data.size() * 4);
    constexpr size_t CHUNK = 65536;
    QByteArray buffer(CHUNK, 0);

    do {
        strm.next_out = reinterpret_cast<uint8_t*>(buffer.data());
        strm.avail_out = CHUNK;
        ret = lzma_code(&strm, LZMA_FINISH);
        output.append(buffer.constData(), static_cast<int>(CHUNK - strm.avail_out));
    } while (ret == LZMA_OK);

    lzma_end(&strm);

    if (ret == LZMA_STREAM_END) {
        LOG_INFO_CAT(LOG_TAG, QString("XZ decompressed: %1 → %2 bytes")
                                  .arg(data.size()).arg(output.size()));
        return output;
    }

    LOG_ERROR_CAT(LOG_TAG, QString("XZ decompression failed: %1").arg(ret));
    return {};
}

QByteArray LzmaDecoder::autoDecompress(const QByteArray& data)
{
    if (isXz(data))
        return decompressXz(data);
    if (isLzma(data))
        return decompress(data);
    return {};
}

} // namespace sakura
