#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QMap>
#include <cstdint>

namespace sakura {

static constexpr uint32_t EROFS_MAGIC = 0xE0F5E1E2;

#pragma pack(push, 1)
struct ErofsSuperBlock {
    uint32_t magic;
    uint32_t checksum;
    uint32_t feature_compat;
    uint8_t  blkszbits;         // block size = 1 << blkszbits
    uint8_t  sb_extslots;
    uint16_t root_nid;
    uint64_t inos;
    uint64_t build_time;
    uint32_t build_time_nsec;
    uint32_t blocks;
    uint32_t meta_blkaddr;
    uint32_t xattr_blkaddr;
    uint8_t  uuid[16];
    uint8_t  volume_name[16];
    uint32_t feature_incompat;
    // ... more fields
};
#pragma pack(pop)

enum class ErofsDataLayout : uint8_t {
    FlatPlain = 0,
    CompressedFull = 1,
    FlatInline = 2,
    CompressedCompact = 3,
    ChunkBased = 4
};

class ErofsParser {
public:
    explicit ErofsParser(const QByteArray& imageData);

    static bool isErofs(const QByteArray& data);
    static bool isErofsFile(const QString& path);

    bool isValid() const { return m_valid; }
    uint32_t blockSize() const { return m_blockSize; }
    QString volumeName() const { return m_volumeName; }

    QByteArray readFileContent(const QString& path);
    QString readTextFile(const QString& path);
    QMap<QString, QString> readBuildProp();
    bool fileExists(const QString& path);
    QStringList listDirectory(const QString& path);

private:
    struct InodeInfo {
        uint16_t mode = 0;
        uint32_t size = 0;
        uint64_t nid = 0;
        ErofsDataLayout layout = ErofsDataLayout::FlatPlain;
        uint32_t rawBlkAddr = 0;
        bool compact = false;
        bool valid = false;
    };

    InodeInfo readInode(uint64_t nid);
    QByteArray readInodeData(const InodeInfo& inode);
    QList<QPair<QString, uint64_t>> readDirectory(uint64_t nid);
    uint64_t findFile(const QString& path);

    const QByteArray& m_data;
    bool m_valid = false;
    uint32_t m_blockSize = 4096;
    uint64_t m_rootNid = 0;
    QString m_volumeName;
};

} // namespace sakura
