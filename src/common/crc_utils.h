#pragma once

#include <cstdint>
#include <cstddef>
#include <QByteArray>

namespace sakura {

class Crc32 {
public:
    static uint32_t compute(const uint8_t* data, size_t length);
    static uint32_t compute(const QByteArray& data);
    static uint32_t update(uint32_t crc, const uint8_t* data, size_t length);

private:
    static const uint32_t s_table[256];
    static void initTable();
};

class Crc16 {
public:
    // CRC16-CCITT (used by HDLC/Qualcomm Diag)
    static uint16_t ccitt(const uint8_t* data, size_t length);
    static uint16_t ccitt(const QByteArray& data);
    static uint16_t ccittUpdate(uint16_t crc, const uint8_t* data, size_t length);

    // Spreadtrum checksum (simple sum-based)
    static uint16_t sprdChecksum(const uint8_t* data, size_t length);
    static uint16_t sprdChecksum(const QByteArray& data);

private:
    static const uint16_t s_ccittTable[256];
};

// MTK-specific checksum
class MtkChecksum {
public:
    static uint16_t compute(const uint8_t* data, size_t length);
    static uint16_t compute(const QByteArray& data);
};

} // namespace sakura
