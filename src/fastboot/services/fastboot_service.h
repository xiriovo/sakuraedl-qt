#pragma once

#include "fastboot/protocol/fastboot_client.h"
#include "transport/usb_transport.h"

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <functional>
#include <memory>
#include <vector>

namespace sakura {

// ---------------------------------------------------------------------------
// Device information snapshot
// ---------------------------------------------------------------------------

struct FastbootDeviceInfo {
    QString serialNumber;
    QString product;
    QString bootloaderVersion;
    QString baseband;
    QString hardwareRevision;
    QString secureState;           // "yes" / "no"
    bool    isUnlocked = false;
    QMap<QString, QString> partitions;  // name → size (as reported by device)

    bool isValid() const { return !serialNumber.isEmpty(); }
};

// ---------------------------------------------------------------------------
// Flash task descriptor (for scripted / batch flashing)
// ---------------------------------------------------------------------------

struct FlashTask {
    QString partition;
    QString filePath;
    bool    erase = true;          // erase before flash?
};

// ---------------------------------------------------------------------------
// FastbootService – high-level service orchestrating Fastboot operations
// ---------------------------------------------------------------------------

class FastbootService : public QObject {
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    explicit FastbootService(QObject* parent = nullptr);
    ~FastbootService() override;

    // --- Device selection & connection -------------------------------------

    /// Enumerate Fastboot devices attached via USB.
    QStringList detectDevices();

    /// Open a specific device by serial number (or first found if empty).
    bool selectDevice(const QString& serial = {});

    /// Disconnect the current device.
    void disconnect();

    /// True if a device is currently selected and connected.
    bool isConnected() const;

    /// Access the underlying client (nullptr if not connected).
    FastbootClient* client() const { return m_client.get(); }

    // --- Device info -------------------------------------------------------

    /// Query all standard bootloader variables and return a snapshot.
    FastbootDeviceInfo refreshDeviceInfo();

    /// Most recent device info (from last refreshDeviceInfo call).
    const FastbootDeviceInfo& deviceInfo() const { return m_deviceInfo; }

    // --- Flash / erase -----------------------------------------------------

    /// Flash a single partition from a file.
    bool flashPartition(const QString& partition, const QString& filePath);

    /// Flash a single partition from in-memory data.
    bool flashPartition(const QString& partition, const QByteArray& data);

    /// Erase a single partition.
    bool erasePartition(const QString& partition);

    /// Execute a list of flash tasks sequentially.
    bool flashScript(const std::vector<FlashTask>& tasks);

    // --- Reboot helpers ----------------------------------------------------

    bool reboot();
    bool rebootBootloader();
    bool rebootRecovery();

    // --- OEM ---------------------------------------------------------------

    bool oemUnlock();
    bool oemLock();

    // --- Configuration -----------------------------------------------------

    void setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }

signals:
    void deviceConnected(const QString& serial);
    void deviceDisconnected();
    void operationProgress(qint64 current, qint64 total);
    void operationInfo(const QString& message);
    void operationFinished(bool success, const QString& message);

private:
    void reportProgress(qint64 current, qint64 total);

    /// Read a file and split into chunks if it exceeds max-download-size.
    QByteArray readImageFile(const QString& path);

    std::unique_ptr<UsbTransport>   m_transport;
    std::unique_ptr<FastbootClient> m_client;
    FastbootDeviceInfo              m_deviceInfo;
    ProgressCallback                m_progressCb;
};

} // namespace sakura
