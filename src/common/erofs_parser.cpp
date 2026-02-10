#include "erofs_parser.h"
#include "lz4_decoder.h"
#include "core/logger.h"
#include <cstring>

namespace sakura {

bool ErofsParser::isErofs(const QByteArray& data)
{
    if (data.size() < 1024 + 4) return false;
    uint32_t magic;
    std::memcpy(&magic, data.constData() + 1024, 4);
    return magic == EROFS_MAGIC;
}

bool ErofsParser::isErofsFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray header = f.read(1028);
    f.close();
    return isErofs(header);
}

ErofsParser::ErofsParser(const QByteArray& imageData)
    : m_data(imageData)
{
    if (!isErofs(m_data)) return;

    const uint8_t* sb = reinterpret_cast<const uint8_t*>(m_data.constData()) + 1024;
    uint8_t blkszbits = sb[8];
    m_blockSize = 1U << blkszbits;

    uint16_t rootNid;
    std::memcpy(&rootNid, sb + 10, 2);
    m_rootNid = rootNid;

    char volName[17] = {};
    std::memcpy(volName, sb + 48, 16);
    m_volumeName = QString::fromLatin1(volName);

    m_valid = true;
    LOG_DEBUG(QString("EROFS: blockSize=%1, rootNid=%2, vol=%3")
                  .arg(m_blockSize).arg(m_rootNid).arg(m_volumeName));
}

ErofsParser::InodeInfo ErofsParser::readInode(uint64_t nid)
{
    InodeInfo info;
    info.nid = nid;

    // EROFS inode is at: meta_blkaddr * blockSize + nid * 32 (compact) or nid * 64 (extended)
    // Simplified: assume compact inodes, nid offset = 1024 (superblock) + nid_offset
    uint64_t offset = 1024 + nid * 32; // This is simplified
    if (offset + 32 > static_cast<uint64_t>(m_data.size())) return info;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(m_data.constData()) + offset;
    uint16_t format;
    std::memcpy(&format, d, 2);

    info.layout = static_cast<ErofsDataLayout>((format >> 1) & 0x7);
    info.compact = !(format & 1); // bit 0 = extended if set

    std::memcpy(&info.mode, d + 2, 2);
    // Read size based on compact/extended
    if (info.compact) {
        std::memcpy(&info.size, d + 8, 4);
        std::memcpy(&info.rawBlkAddr, d + 16, 4);
    } else {
        std::memcpy(&info.size, d + 8, 4);
        std::memcpy(&info.rawBlkAddr, d + 16, 4);
    }

    info.valid = true;
    return info;
}

QByteArray ErofsParser::readInodeData(const InodeInfo& inode)
{
    if (!inode.valid || inode.size == 0) return {};

    switch (inode.layout) {
    case ErofsDataLayout::FlatPlain: {
        uint64_t offset = static_cast<uint64_t>(inode.rawBlkAddr) * m_blockSize;
        if (offset + inode.size > static_cast<uint64_t>(m_data.size())) return {};
        return m_data.mid(offset, inode.size);
    }
    case ErofsDataLayout::FlatInline: {
        // Data is inline after the inode
        uint64_t inodeOffset = 1024 + inode.nid * 32;
        uint64_t dataOffset = inodeOffset + (inode.compact ? 32 : 64);
        if (dataOffset + inode.size > static_cast<uint64_t>(m_data.size())) return {};
        return m_data.mid(dataOffset, inode.size);
    }
    case ErofsDataLayout::CompressedFull:
    case ErofsDataLayout::CompressedCompact: {
        // Compressed data - need LZ4 decompression
        uint64_t offset = static_cast<uint64_t>(inode.rawBlkAddr) * m_blockSize;
        // Read one block of compressed data (simplified)
        if (offset + m_blockSize > static_cast<uint64_t>(m_data.size())) return {};
        QByteArray compressed = m_data.mid(offset, m_blockSize);
        return Lz4Decoder::decompressBlock(compressed, inode.size);
    }
    default:
        return {};
    }
}

QList<QPair<QString, uint64_t>> ErofsParser::readDirectory(uint64_t nid)
{
    QList<QPair<QString, uint64_t>> entries;
    InodeInfo inode = readInode(nid);
    QByteArray data = readInodeData(inode);
    if (data.isEmpty()) return entries;

    int pos = 0;
    while (pos + 12 <= data.size()) {
        uint64_t childNid;
        uint16_t nameOff;
        uint8_t fileType;

        std::memcpy(&childNid, data.constData() + pos, 8);
        std::memcpy(&nameOff, data.constData() + pos + 8, 2);
        fileType = static_cast<uint8_t>(data[pos + 10]);
        Q_UNUSED(fileType);

        // Calculate name length from next entry's nameOff or end of block
        int nextNameOff = data.size(); // default: to end
        if (pos + 12 + 12 <= data.size()) {
            uint16_t nextOff;
            std::memcpy(&nextOff, data.constData() + pos + 12 + 8, 2);
            if (nextOff > nameOff && nextOff <= data.size())
                nextNameOff = nextOff;
        }

        int nameLen = nextNameOff - nameOff;
        if (nameOff + nameLen <= data.size() && nameLen > 0 && nameLen < 256) {
            QString name = QString::fromUtf8(data.constData() + nameOff, nameLen);
            if (name != "." && name != "..")
                entries.append({name, childNid});
        }

        pos += 12;
    }
    return entries;
}

uint64_t ErofsParser::findFile(const QString& path)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    uint64_t currentNid = m_rootNid;

    for (const auto& part : parts) {
        auto entries = readDirectory(currentNid);
        bool found = false;
        for (const auto& [name, nid] : entries) {
            if (name == part) {
                currentNid = nid;
                found = true;
                break;
            }
        }
        if (!found) return 0;
    }
    return currentNid;
}

QByteArray ErofsParser::readFileContent(const QString& path)
{
    uint64_t nid = findFile(path);
    if (nid == 0) return {};
    return readInodeData(readInode(nid));
}

QString ErofsParser::readTextFile(const QString& path)
{
    return QString::fromUtf8(readFileContent(path));
}

QMap<QString, QString> ErofsParser::readBuildProp()
{
    QMap<QString, QString> props;
    QStringList paths = {"system/build.prop", "build.prop", "default.prop",
                         "vendor/build.prop", "product/build.prop"};

    for (const auto& p : paths) {
        QString content = readTextFile(p);
        if (content.isEmpty()) continue;
        for (const auto& line : content.split('\n')) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
            int eq = trimmed.indexOf('=');
            if (eq > 0)
                props[trimmed.left(eq).trimmed()] = trimmed.mid(eq + 1).trimmed();
        }
    }
    return props;
}

bool ErofsParser::fileExists(const QString& path)
{
    return findFile(path) != 0;
}

QStringList ErofsParser::listDirectory(const QString& path)
{
    uint64_t nid = path.isEmpty() || path == "/" ? m_rootNid : findFile(path);
    if (nid == 0) return {};
    QStringList result;
    for (const auto& [name, id] : readDirectory(nid)) {
        Q_UNUSED(id);
        result.append(name);
    }
    return result;
}

} // namespace sakura
