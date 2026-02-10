#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QMap>
#include <cstdint>

namespace sakura {

#pragma pack(push, 1)
struct Ext4SuperBlock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;       // block_size = 1024 << s_log_block_size
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;                // 0xEF53
    // ... more fields omitted for brevity
    uint8_t  _padding[1024 - 60];    // fill to 1024 bytes
};

struct Ext4Inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];           // block pointers / extent tree
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    // ... more fields
};

struct Ext4DirEntry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[256]; // variable length
};
#pragma pack(pop)

enum class Ext4FileType : uint8_t {
    Unknown = 0,
    RegularFile = 1,
    Directory = 2,
    CharDevice = 3,
    BlockDevice = 4,
    Fifo = 5,
    Socket = 6,
    SymbolicLink = 7
};

class Ext4Parser {
public:
    explicit Ext4Parser(const QByteArray& imageData);

    static bool isExt4(const QByteArray& data);
    static bool isExt4File(const QString& path);

    bool isValid() const { return m_valid; }
    uint32_t blockSize() const { return m_blockSize; }
    QString volumeName() const { return m_volumeName; }

    // File operations
    QByteArray readFileContent(const QString& path);
    QString readTextFile(const QString& path);
    QMap<QString, QString> readBuildProp();
    bool fileExists(const QString& path);
    QStringList listDirectory(const QString& path);

private:
    Ext4Inode readInode(uint32_t inodeNum);
    QByteArray readInodeData(const Ext4Inode& inode);
    QList<QPair<QString, uint32_t>> readDirectory(uint32_t inodeNum);
    uint32_t findFile(const QString& path);

    QByteArray readBlock(uint64_t blockNum);
    QByteArray readExtentData(const Ext4Inode& inode);
    QByteArray readIndirectData(const Ext4Inode& inode);

    const QByteArray& m_data;
    bool m_valid = false;
    uint32_t m_blockSize = 4096;
    uint32_t m_inodeSize = 256;
    uint32_t m_inodesPerGroup = 0;
    uint32_t m_blocksPerGroup = 0;
    QString m_volumeName;
};

} // namespace sakura
