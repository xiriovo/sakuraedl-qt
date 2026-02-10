#include "ext4_parser.h"
#include "core/logger.h"
#include <cstring>

namespace sakura {

static constexpr uint16_t EXT4_MAGIC = 0xEF53;
static constexpr uint32_t EXT4_EXTENT_MAGIC = 0xF30A;

bool Ext4Parser::isExt4(const QByteArray& data)
{
    if (data.size() < 2048) return false;
    uint16_t magic;
    std::memcpy(&magic, data.constData() + 1024 + 56, 2); // superblock at offset 1024, magic at +56
    return magic == EXT4_MAGIC;
}

bool Ext4Parser::isExt4File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray header = f.read(2048);
    f.close();
    return isExt4(header);
}

Ext4Parser::Ext4Parser(const QByteArray& imageData)
    : m_data(imageData)
{
    if (!isExt4(m_data)) return;

    // Parse superblock
    const uint8_t* sb = reinterpret_cast<const uint8_t*>(m_data.constData()) + 1024;
    uint32_t logBlockSize;
    std::memcpy(&logBlockSize, sb + 24, 4);
    m_blockSize = 1024U << logBlockSize;

    std::memcpy(&m_inodesPerGroup, sb + 40, 4);
    std::memcpy(&m_blocksPerGroup, sb + 32, 4);

    // Inode size (at offset 88 in superblock)
    std::memcpy(&m_inodeSize, sb + 88, 2);
    if (m_inodeSize == 0) m_inodeSize = 128;

    // Volume name (at offset 120, 16 bytes)
    char volName[17] = {};
    std::memcpy(volName, sb + 120, 16);
    m_volumeName = QString::fromLatin1(volName);

    m_valid = true;
    LOG_DEBUG(QString("EXT4: blockSize=%1, inodeSize=%2, vol=%3")
                  .arg(m_blockSize).arg(m_inodeSize).arg(m_volumeName));
}

QByteArray Ext4Parser::readBlock(uint64_t blockNum)
{
    uint64_t offset = blockNum * m_blockSize;
    if (offset + m_blockSize > static_cast<uint64_t>(m_data.size()))
        return {};
    return m_data.mid(offset, m_blockSize);
}

Ext4Inode Ext4Parser::readInode(uint32_t inodeNum)
{
    Ext4Inode inode = {};
    if (inodeNum == 0 || !m_valid) return inode;

    uint32_t group = (inodeNum - 1) / m_inodesPerGroup;
    uint32_t index = (inodeNum - 1) % m_inodesPerGroup;

    // Read group descriptor to find inode table block
    uint64_t gdtOffset = (m_blockSize <= 1024 ? 2048 : m_blockSize) + group * 32;
    if (gdtOffset + 32 > static_cast<uint64_t>(m_data.size())) return inode;

    uint32_t inodeTableBlock;
    std::memcpy(&inodeTableBlock, m_data.constData() + gdtOffset + 8, 4);

    uint64_t inodeOffset = static_cast<uint64_t>(inodeTableBlock) * m_blockSize +
                           static_cast<uint64_t>(index) * m_inodeSize;
    if (inodeOffset + sizeof(Ext4Inode) > static_cast<uint64_t>(m_data.size()))
        return inode;

    std::memcpy(&inode, m_data.constData() + inodeOffset,
                qMin(static_cast<size_t>(m_inodeSize), sizeof(Ext4Inode)));
    return inode;
}

QByteArray Ext4Parser::readInodeData(const Ext4Inode& inode)
{
    uint64_t size = inode.i_size_lo | (static_cast<uint64_t>(inode.i_size_high) << 32);
    if (size == 0) return {};

    // Check if using extents (flag bit 19)
    if (inode.i_flags & 0x80000) {
        return readExtentData(inode);
    } else {
        return readIndirectData(inode);
    }
}

QByteArray Ext4Parser::readExtentData(const Ext4Inode& inode)
{
    uint64_t fileSize = inode.i_size_lo | (static_cast<uint64_t>(inode.i_size_high) << 32);
    QByteArray result;
    result.reserve(static_cast<int>(qMin(fileSize, static_cast<uint64_t>(64 * 1024 * 1024))));

    // Extent header is in i_block[0..3]
    const uint8_t* extData = reinterpret_cast<const uint8_t*>(inode.i_block);
    uint16_t magic, entries;
    std::memcpy(&magic, extData, 2);
    std::memcpy(&entries, extData + 2, 2);

    if (magic != EXT4_EXTENT_MAGIC) return {};

    uint16_t depth;
    std::memcpy(&depth, extData + 6, 2);

    if (depth == 0) {
        // Leaf extents directly in inode
        for (uint16_t i = 0; i < entries; i++) {
            const uint8_t* ext = extData + 12 + i * 12;
            uint16_t len;    std::memcpy(&len, ext + 4, 2);
            uint16_t startHi; std::memcpy(&startHi, ext + 6, 2);
            uint32_t startLo; std::memcpy(&startLo, ext + 8, 4);
            uint64_t physBlock = (static_cast<uint64_t>(startHi) << 32) | startLo;

            // Handle uninitialized extents (bit 15 of length)
            uint16_t actualLen = len & 0x7FFF;
            for (uint16_t b = 0; b < actualLen && static_cast<uint64_t>(result.size()) < fileSize; b++) {
                QByteArray block = readBlock(physBlock + b);
                if (block.isEmpty()) break;
                result.append(block);
            }
        }
    } else {
        // Internal extent index nodes (depth > 0)
        // Each index entry points to a block containing the next level of the extent tree.
        // Format: [logical_block(4)][leaf_lo(4)][leaf_hi(2)][unused(2)] = 12 bytes each
        for (uint16_t i = 0; i < entries; i++) {
            const uint8_t* idx = extData + 12 + i * 12;
            uint32_t leafLo; std::memcpy(&leafLo, idx + 4, 4);
            uint16_t leafHi; std::memcpy(&leafHi, idx + 8, 2);
            uint64_t leafBlock = (static_cast<uint64_t>(leafHi) << 32) | leafLo;

            // Read the block containing the next level of extents
            QByteArray childBlock = readBlock(leafBlock);
            if (childBlock.isEmpty() || childBlock.size() < 12) continue;

            const uint8_t* childData = reinterpret_cast<const uint8_t*>(childBlock.constData());
            uint16_t childMagic, childEntries, childDepth;
            std::memcpy(&childMagic, childData, 2);
            std::memcpy(&childEntries, childData + 2, 2);
            std::memcpy(&childDepth, childData + 6, 2);

            if (childMagic != EXT4_EXTENT_MAGIC) continue;

            if (childDepth == 0) {
                // Leaf level — read actual data extents
                for (uint16_t j = 0; j < childEntries; j++) {
                    const uint8_t* ext = childData + 12 + j * 12;
                    uint16_t len;    std::memcpy(&len, ext + 4, 2);
                    uint16_t sHi;   std::memcpy(&sHi, ext + 6, 2);
                    uint32_t sLo;   std::memcpy(&sLo, ext + 8, 4);
                    uint64_t phys = (static_cast<uint64_t>(sHi) << 32) | sLo;
                    uint16_t actualLen = len & 0x7FFF;

                    for (uint16_t b = 0; b < actualLen &&
                         static_cast<uint64_t>(result.size()) < fileSize; b++) {
                        QByteArray blk = readBlock(phys + b);
                        if (blk.isEmpty()) break;
                        result.append(blk);
                    }
                }
            }
            // For depth > 1, further recursion would be needed.
            // In practice, ext4 rarely uses depth > 1 for typical file sizes.
        }
    }

    result.resize(qMin(static_cast<qint64>(fileSize), static_cast<qint64>(result.size())));
    return result;
}

QByteArray Ext4Parser::readIndirectData(const Ext4Inode& inode)
{
    uint64_t fileSize = inode.i_size_lo | (static_cast<uint64_t>(inode.i_size_high) << 32);
    QByteArray result;
    uint32_t ptrsPerBlock = m_blockSize / 4; // Number of block pointers per block

    // Direct blocks (0-11)
    for (int i = 0; i < 12 && static_cast<uint64_t>(result.size()) < fileSize; i++) {
        if (inode.i_block[i] == 0) continue;
        result.append(readBlock(inode.i_block[i]));
    }

    // Single indirect block (i_block[12])
    // Contains an array of block numbers pointing to data blocks
    if (inode.i_block[12] != 0 && static_cast<uint64_t>(result.size()) < fileSize) {
        QByteArray indirectBlock = readBlock(inode.i_block[12]);
        if (!indirectBlock.isEmpty()) {
            const uint32_t* ptrs = reinterpret_cast<const uint32_t*>(indirectBlock.constData());
            for (uint32_t j = 0; j < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; j++) {
                if (ptrs[j] == 0) continue;
                result.append(readBlock(ptrs[j]));
            }
        }
    }

    // Double indirect block (i_block[13])
    // Points to blocks containing arrays of single-indirect block pointers
    if (inode.i_block[13] != 0 && static_cast<uint64_t>(result.size()) < fileSize) {
        QByteArray dindBlock = readBlock(inode.i_block[13]);
        if (!dindBlock.isEmpty()) {
            const uint32_t* l1ptrs = reinterpret_cast<const uint32_t*>(dindBlock.constData());
            for (uint32_t j = 0; j < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; j++) {
                if (l1ptrs[j] == 0) continue;
                QByteArray l2block = readBlock(l1ptrs[j]);
                if (l2block.isEmpty()) continue;
                const uint32_t* l2ptrs = reinterpret_cast<const uint32_t*>(l2block.constData());
                for (uint32_t k = 0; k < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; k++) {
                    if (l2ptrs[k] == 0) continue;
                    result.append(readBlock(l2ptrs[k]));
                }
            }
        }
    }

    // Triple indirect block (i_block[14])
    // Points to blocks containing arrays of double-indirect block pointers
    if (inode.i_block[14] != 0 && static_cast<uint64_t>(result.size()) < fileSize) {
        QByteArray tindBlock = readBlock(inode.i_block[14]);
        if (!tindBlock.isEmpty()) {
            const uint32_t* l1ptrs = reinterpret_cast<const uint32_t*>(tindBlock.constData());
            for (uint32_t j = 0; j < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; j++) {
                if (l1ptrs[j] == 0) continue;
                QByteArray l2block = readBlock(l1ptrs[j]);
                if (l2block.isEmpty()) continue;
                const uint32_t* l2ptrs = reinterpret_cast<const uint32_t*>(l2block.constData());
                for (uint32_t k = 0; k < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; k++) {
                    if (l2ptrs[k] == 0) continue;
                    QByteArray l3block = readBlock(l2ptrs[k]);
                    if (l3block.isEmpty()) continue;
                    const uint32_t* l3ptrs = reinterpret_cast<const uint32_t*>(l3block.constData());
                    for (uint32_t l = 0; l < ptrsPerBlock && static_cast<uint64_t>(result.size()) < fileSize; l++) {
                        if (l3ptrs[l] == 0) continue;
                        result.append(readBlock(l3ptrs[l]));
                    }
                }
            }
        }
    }

    result.resize(qMin(static_cast<qint64>(fileSize), static_cast<qint64>(result.size())));
    return result;
}

QList<QPair<QString, uint32_t>> Ext4Parser::readDirectory(uint32_t inodeNum)
{
    QList<QPair<QString, uint32_t>> entries;
    Ext4Inode inode = readInode(inodeNum);
    QByteArray data = readInodeData(inode);

    int pos = 0;
    while (pos + 8 <= data.size()) {
        uint32_t entInode;
        uint16_t recLen;
        uint8_t nameLen;
        std::memcpy(&entInode, data.constData() + pos, 4);
        std::memcpy(&recLen, data.constData() + pos + 4, 2);
        nameLen = static_cast<uint8_t>(data[pos + 6]);

        if (recLen == 0) break;
        if (entInode != 0 && nameLen > 0 && pos + 8 + nameLen <= data.size()) {
            QString name = QString::fromLatin1(data.constData() + pos + 8, nameLen);
            if (name != "." && name != "..")
                entries.append({name, entInode});
        }
        pos += recLen;
    }
    return entries;
}

uint32_t Ext4Parser::findFile(const QString& path)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    uint32_t currentInode = 2; // Root inode

    for (const auto& part : parts) {
        auto entries = readDirectory(currentInode);
        bool found = false;
        for (const auto& [name, ino] : entries) {
            if (name == part) {
                currentInode = ino;
                found = true;
                break;
            }
        }
        if (!found) return 0;
    }
    return currentInode;
}

QByteArray Ext4Parser::readFileContent(const QString& path)
{
    uint32_t inodeNum = findFile(path);
    if (inodeNum == 0) return {};
    return readInodeData(readInode(inodeNum));
}

QString Ext4Parser::readTextFile(const QString& path)
{
    return QString::fromUtf8(readFileContent(path));
}

QMap<QString, QString> Ext4Parser::readBuildProp()
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
            if (eq > 0) {
                props[trimmed.left(eq).trimmed()] = trimmed.mid(eq + 1).trimmed();
            }
        }
    }
    return props;
}

bool Ext4Parser::fileExists(const QString& path)
{
    return findFile(path) != 0;
}

QStringList Ext4Parser::listDirectory(const QString& path)
{
    uint32_t inodeNum = path.isEmpty() || path == "/" ? 2 : findFile(path);
    if (inodeNum == 0) return {};

    QStringList result;
    for (const auto& [name, ino] : readDirectory(inodeNum)) {
        Q_UNUSED(ino);
        result.append(name);
    }
    return result;
}

} // namespace sakura
