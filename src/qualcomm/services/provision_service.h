#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <cstdint>

namespace sakura {

class FirehoseClient;

// ─── UFS LUN configuration entry ────────────────────────────────────
struct UfsLunConfig {
    uint32_t lunNumber = 0;
    uint64_t capacity = 0;          // In bytes
    QString  desc;                  // Description
    bool     bootable = false;
    bool     writeProtect = false;
    uint32_t memoryType = 0;        // 0=Normal, 1=Enhanced
    uint32_t logicalBlockSize = 4096;
};

// ─── eMMC provision configuration ────────────────────────────────────
struct EmmcProvisionConfig {
    uint64_t enhancedAreaStart = 0;
    uint64_t enhancedAreaSize = 0;
    uint32_t gpPartitionSize[4] = {};
    bool     reliableWrite = false;
};

// ─── Provision service ───────────────────────────────────────────────
// Generates and sends UFS/eMMC provisioning XML to Firehose.
// Provisioning sets up the storage layout before any data is written.
class ProvisionService : public QObject {
    Q_OBJECT

public:
    explicit ProvisionService(QObject* parent = nullptr);

    // ── UFS provisioning ─────────────────────────────────────────────
    bool provisionUfs(FirehoseClient* client, const QList<UfsLunConfig>& luns);
    QList<UfsLunConfig> readCurrentUfsConfig(FirehoseClient* client);
    QString generateUfsProvisionXml(const QList<UfsLunConfig>& luns);

    // ── eMMC provisioning ────────────────────────────────────────────
    bool provisionEmmc(FirehoseClient* client, const EmmcProvisionConfig& config);
    QString generateEmmcProvisionXml(const EmmcProvisionConfig& config);

    // ── Common ───────────────────────────────────────────────────────
    bool applyProvisionXml(FirehoseClient* client, const QString& xml);

    // ── Presets ──────────────────────────────────────────────────────
    static QList<UfsLunConfig> defaultUfsLayout6Lun();
    static QList<UfsLunConfig> defaultUfsLayout5Lun();

signals:
    void statusMessage(const QString& message);
    void errorOccurred(const QString& error);

private:
    static constexpr uint32_t MAX_UFS_LUNS = 8;
};

} // namespace sakura
