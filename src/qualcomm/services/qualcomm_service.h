#pragma once

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QString>
#include <memory>
#include <functional>

#include "common/partition_info.h"
#include "qualcomm/protocol/sahara_protocol.h"
#include "qualcomm/protocol/firehose_client.h"

namespace sakura {

class ITransport;
class IAuthStrategy;

// ─── High-level Qualcomm EDL service ─────────────────────────────────
// Orchestrates Sahara handshake, loader upload, and Firehose operations.
class QualcommService : public QObject {
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    enum class DeviceState {
        Disconnected,
        SaharaMode,
        FirehoseMode,
        Ready,
        Error,
    };

    explicit QualcommService(QObject* parent = nullptr);
    ~QualcommService();

    // ── Connection lifecycle ─────────────────────────────────────────
    bool connectDevice(ITransport* transport);
    bool connectFirehoseDirect(ITransport* transport);
    void disconnect();
    DeviceState state() const { return m_state; }

    // ── Sahara operations ────────────────────────────────────────────
    SaharaDeviceInfo deviceInfo() const { return m_deviceInfo; }
    bool uploadLoader(const QByteArray& loaderData);

    // ── Authentication ───────────────────────────────────────────────
    void setAuthStrategy(std::shared_ptr<IAuthStrategy> auth);
    bool authenticate();

    // ── Firehose partition operations ────────────────────────────────
    QList<PartitionInfo> readPartitions(uint32_t lun = 0);
    QByteArray readPartition(const QString& name, uint32_t lun = 0,
                             ProgressCallback progress = nullptr);
    bool writePartition(const QString& name, const QByteArray& data,
                        uint32_t lun = 0, ProgressCallback progress = nullptr);
    bool erasePartition(const QString& name, uint32_t lun = 0);

    // ── Device control ───────────────────────────────────────────────
    bool reboot();
    bool powerOff();
    bool setActiveSlot(const QString& slot);

    // ── Configuration ────────────────────────────────────────────────
    void setStorageType(FirehoseStorageType type);
    void setMaxPayloadSize(uint32_t size);

    // ── Raw access ───────────────────────────────────────────────────
    SaharaClient* saharaClient() { return m_sahara.get(); }
    FirehoseClient* firehoseClient() { return m_firehose.get(); }

signals:
    void stateChanged(int state);
    void deviceInfoReady(const QVariantMap& info);
    void partitionsReady(const QList<PartitionInfo>& partitions);
    void transferProgress(qint64 current, qint64 total);
    void statusMessage(const QString& message);
    void errorOccurred(const QString& error);

private:
    void setState(DeviceState state);
    bool enterFirehoseMode();

    std::unique_ptr<SaharaClient> m_sahara;
    std::unique_ptr<FirehoseClient> m_firehose;
    std::shared_ptr<IAuthStrategy> m_authStrategy;

    ITransport* m_transport = nullptr;
    DeviceState m_state = DeviceState::Disconnected;
    SaharaDeviceInfo m_deviceInfo;
    QByteArray m_loaderData;

    FirehoseStorageType m_storageType = FirehoseStorageType::UFS;
    uint32_t m_maxPayloadSize = 1048576;
};

} // namespace sakura
