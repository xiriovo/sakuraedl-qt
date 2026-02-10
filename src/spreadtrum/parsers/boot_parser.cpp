#include "boot_parser.h"
#include "core/logger.h"

#include <QFile>
#include <cstring>

namespace sakura {

static constexpr char LOG_TAG[] = "SPRD-BOOT";

// ── Parsing ─────────────────────────────────────────────────────────────────

bool BootParser::parse(const QByteArray& imageData)
{
    m_info = {};
    m_error.clear();

    if (!isBootImage(imageData)) {
        m_error = "Not a valid Android boot image (missing ANDROID! magic)";
        return false;
    }

    uint32_t version = detectHeaderVersion(imageData);

    if (version >= 3) {
        return parseV3(imageData);
    }

    return parseV0V1V2(imageData);
}

bool BootParser::parseFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QString("Cannot open file: %1").arg(file.errorString());
        return false;
    }

    // Read enough for the header + some content
    QByteArray data = file.readAll();
    file.close();

    return parse(data);
}

bool BootParser::parseV0V1V2(const QByteArray& data)
{
    if (data.size() < static_cast<int>(sizeof(BootImageHeader))) {
        m_error = "Image too small for boot header";
        return false;
    }

    const auto* hdr = reinterpret_cast<const BootImageHeader*>(data.constData());

    m_info.headerVersion = hdr->headerVersion;
    m_info.pageSize      = hdr->pageSize;
    m_info.kernelSize    = hdr->kernelSize;
    m_info.ramdiskSize   = hdr->ramdiskSize;
    m_info.secondSize    = hdr->secondSize;
    m_info.kernelAddr    = hdr->kernelAddr;
    m_info.ramdiskAddr   = hdr->ramdiskAddr;
    m_info.secondAddr    = hdr->secondAddr;
    m_info.tagsAddr      = hdr->tagsAddr;
    m_info.name          = QString::fromLatin1(hdr->name, strnlen(hdr->name, 16));
    m_info.cmdline       = QString::fromLatin1(hdr->cmdline, strnlen(hdr->cmdline, 512));

    if (m_info.pageSize == 0 || m_info.pageSize > 0x10000) {
        m_error = QString("Invalid page size: %1").arg(m_info.pageSize);
        return false;
    }

    // Calculate offsets (everything page-aligned after header)
    m_info.kernelOffset  = m_info.pageSize; // kernel starts at page 1
    m_info.ramdiskOffset = alignToPage(m_info.kernelOffset + m_info.kernelSize);
    m_info.secondOffset  = alignToPage(m_info.ramdiskOffset + m_info.ramdiskSize);

    // V1 extras
    if (m_info.headerVersion >= 1) {
        int v1Offset = sizeof(BootImageHeader);
        if (data.size() >= v1Offset + static_cast<int>(sizeof(BootImageHeaderV1Extra))) {
            const auto* v1 = reinterpret_cast<const BootImageHeaderV1Extra*>(
                data.constData() + v1Offset);
            m_info.recoveryDtboSize   = v1->recoveryDtboSize;
            m_info.recoveryDtboOffset = v1->recoveryDtboOffset;
        }
    }

    // V2 extras
    if (m_info.headerVersion >= 2) {
        int v2Offset = sizeof(BootImageHeader) + sizeof(BootImageHeaderV1Extra);
        if (data.size() >= v2Offset + static_cast<int>(sizeof(BootImageHeaderV2Extra))) {
            const auto* v2 = reinterpret_cast<const BootImageHeaderV2Extra*>(
                data.constData() + v2Offset);
            m_info.dtbSize = v2->dtbSize;
            m_info.dtbOffset = alignToPage(m_info.secondOffset + m_info.secondSize);
        }
    }

    LOG_INFO_CAT(LOG_TAG, QString("Boot v%1: kernel=%2 ramdisk=%3 page=%4")
                              .arg(m_info.headerVersion)
                              .arg(m_info.kernelSize)
                              .arg(m_info.ramdiskSize)
                              .arg(m_info.pageSize));
    return true;
}

bool BootParser::parseV3(const QByteArray& data)
{
    if (data.size() < static_cast<int>(sizeof(BootImageHeaderV3))) {
        m_error = "Image too small for V3 boot header";
        return false;
    }

    const auto* hdr = reinterpret_cast<const BootImageHeaderV3*>(data.constData());

    m_info.headerVersion = hdr->headerVersion;
    m_info.pageSize      = 4096; // V3 always uses 4K pages
    m_info.kernelSize    = hdr->kernelSize;
    m_info.ramdiskSize   = hdr->ramdiskSize;
    m_info.cmdline       = QString::fromLatin1(hdr->cmdline, strnlen(hdr->cmdline, 1536));

    // V3 offsets
    m_info.kernelOffset  = alignToPage(hdr->headerSize);
    m_info.ramdiskOffset = alignToPage(m_info.kernelOffset + m_info.kernelSize);

    LOG_INFO_CAT(LOG_TAG, QString("Boot v%1: kernel=%2 ramdisk=%3")
                              .arg(m_info.headerVersion)
                              .arg(m_info.kernelSize)
                              .arg(m_info.ramdiskSize));
    return true;
}

// ── Component extraction ────────────────────────────────────────────────────

QByteArray BootParser::extractKernel(const QByteArray& imageData) const
{
    if (!m_info.isValid() || m_info.kernelSize == 0)
        return {};

    return imageData.mid(static_cast<int>(m_info.kernelOffset),
                         static_cast<int>(m_info.kernelSize));
}

QByteArray BootParser::extractRamdisk(const QByteArray& imageData) const
{
    if (!m_info.isValid() || m_info.ramdiskSize == 0)
        return {};

    return imageData.mid(static_cast<int>(m_info.ramdiskOffset),
                         static_cast<int>(m_info.ramdiskSize));
}

QByteArray BootParser::extractSecond(const QByteArray& imageData) const
{
    if (!m_info.isValid() || m_info.secondSize == 0)
        return {};

    return imageData.mid(static_cast<int>(m_info.secondOffset),
                         static_cast<int>(m_info.secondSize));
}

QByteArray BootParser::extractDtb(const QByteArray& imageData) const
{
    if (!m_info.isValid() || m_info.dtbSize == 0 || m_info.headerVersion < 2)
        return {};

    return imageData.mid(static_cast<int>(m_info.dtbOffset),
                         static_cast<int>(m_info.dtbSize));
}

// ── Static utilities ────────────────────────────────────────────────────────

bool BootParser::isBootImage(const QByteArray& data)
{
    if (data.size() < BOOT_MAGIC_SIZE)
        return false;
    return std::memcmp(data.constData(), BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0;
}

uint32_t BootParser::detectHeaderVersion(const QByteArray& data)
{
    if (data.size() < static_cast<int>(sizeof(BootImageHeader)))
        return 0;

    const auto* hdr = reinterpret_cast<const BootImageHeader*>(data.constData());
    return hdr->headerVersion;
}

uint64_t BootParser::alignToPage(uint64_t offset) const
{
    if (m_info.pageSize == 0)
        return offset;
    return ((offset + m_info.pageSize - 1) / m_info.pageSize) * m_info.pageSize;
}

} // namespace sakura
