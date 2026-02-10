#pragma once

#include "fastboot/protocol/fastboot_client.h"

#include <QMap>
#include <QObject>
#include <QString>

namespace sakura {

// ---------------------------------------------------------------------------
// Huawei / Honor vendor-specific Fastboot extensions
//
// Huawei bootloaders use several non-standard OEM commands for device info
// retrieval, FRP unlocking, and bootloader unlock via unlock code.
// ---------------------------------------------------------------------------

struct HuaweiDeviceInfo {
    QString imei;
    QString model;
    QString productId;
    QString boardId;
    QString softwareVersion;
    QString serialNumber;
    bool    frpLocked   = false;
    bool    blUnlocked  = false;
};

class HuaweiHonorSupport : public QObject {
    Q_OBJECT

public:
    /// Construct with a connected FastbootClient.
    explicit HuaweiHonorSupport(FastbootClient* client, QObject* parent = nullptr);
    ~HuaweiHonorSupport() override = default;

    // --- Device info -------------------------------------------------------

    /// Read Huawei-specific device information via OEM getvar commands.
    HuaweiDeviceInfo readDeviceInfo();

    // --- FRP (Factory Reset Protection) ------------------------------------

    /// Unlock FRP via the Huawei OEM command.
    /// Returns true on success.
    bool unlockFrp();

    // --- Bootloader unlock -------------------------------------------------

    /// Unlock the bootloader using a 16-digit unlock code.
    /// Huawei devices require an OEM-specific command sequence.
    bool unlockBootloaderWithCode(const QString& code);

    // --- Utility -----------------------------------------------------------

    /// Check whether the connected device appears to be a Huawei / Honor.
    bool isHuaweiDevice() const;

    /// Get the unlock code token (used to request unlock code from Huawei).
    /// Returns a hex string, or empty on failure.
    QString getUnlockToken();

signals:
    void infoMessage(const QString& message);

private:
    /// Send a Huawei OEM command and collect INFO messages.
    FastbootResponse sendHuaweiOem(const QString& subcmd);

    FastbootClient* m_client = nullptr;
};

} // namespace sakura
