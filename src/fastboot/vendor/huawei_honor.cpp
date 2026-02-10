#include "huawei_honor.h"
#include "fastboot/protocol/fastboot_protocol.h"
#include "core/logger.h"

namespace sakura {

static constexpr const char* TAG = "HuaweiHonor";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HuaweiHonorSupport::HuaweiHonorSupport(FastbootClient* client, QObject* parent)
    : QObject(parent)
    , m_client(client)
{
    Q_ASSERT(client);
}

// ---------------------------------------------------------------------------
// Device info
// ---------------------------------------------------------------------------

HuaweiDeviceInfo HuaweiHonorSupport::readDeviceInfo()
{
    HuaweiDeviceInfo info;
    if (!m_client || !m_client->isConnected())
        return info;

    // Standard fastboot variables
    info.serialNumber = m_client->getVariable(QStringLiteral("serialno"));
    info.model        = m_client->getVariable(QStringLiteral("product"));

    // Huawei-specific OEM variables
    info.imei            = m_client->getVariable(QStringLiteral("nve:WVLOCK"));
    info.productId       = m_client->getVariable(QStringLiteral("product-id"));
    info.boardId         = m_client->getVariable(QStringLiteral("board-id"));
    info.softwareVersion = m_client->getVariable(QStringLiteral("version-baseband"));

    // Check unlock / FRP state
    QString unlocked = m_client->getVariable(QStringLiteral("unlocked"));
    info.blUnlocked = (unlocked.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);

    QString frp = m_client->getVariable(QStringLiteral("frp-state"));
    info.frpLocked = (frp.compare(QStringLiteral("locked"), Qt::CaseInsensitive) == 0 ||
                      frp.compare(QStringLiteral("1"), Qt::CaseInsensitive) == 0);

    LOG_INFO_CAT(TAG, QStringLiteral("Huawei device: model=%1 serial=%2 unlocked=%3")
                          .arg(info.model, info.serialNumber)
                          .arg(info.blUnlocked ? QStringLiteral("yes") : QStringLiteral("no")));

    return info;
}

// ---------------------------------------------------------------------------
// FRP unlock
// ---------------------------------------------------------------------------

bool HuaweiHonorSupport::unlockFrp()
{
    LOG_INFO_CAT(TAG, "Attempting FRP unlock...");
    emit infoMessage(QStringLiteral("Unlocking FRP..."));

    // Huawei uses: oem frp-unlock
    FastbootResponse resp = sendHuaweiOem(QStringLiteral("frp-unlock"));
    if (resp.isOkay()) {
        LOG_INFO_CAT(TAG, "FRP unlock successful");
        emit infoMessage(QStringLiteral("FRP unlocked"));
        return true;
    }

    LOG_ERROR_CAT(TAG, QStringLiteral("FRP unlock failed: %1").arg(resp.toString()));
    emit infoMessage(QStringLiteral("FRP unlock failed: %1")
                         .arg(QString::fromUtf8(resp.data)));
    return false;
}

// ---------------------------------------------------------------------------
// Bootloader unlock with code
// ---------------------------------------------------------------------------

bool HuaweiHonorSupport::unlockBootloaderWithCode(const QString& code)
{
    if (code.isEmpty()) {
        LOG_ERROR_CAT(TAG, "Unlock code is empty");
        return false;
    }

    LOG_INFO_CAT(TAG, QStringLiteral("Attempting bootloader unlock with code: %1").arg(code));
    emit infoMessage(QStringLiteral("Sending unlock code..."));

    // Huawei bootloader unlock sequence:
    //   1. oem unlock <code>
    //   (Some devices use: oem unlock <code> or oem nv_unlock <code>)
    FastbootResponse resp = m_client->sendCommand(
        QStringLiteral("oem unlock %1").arg(code));

    if (resp.isOkay()) {
        LOG_INFO_CAT(TAG, "Bootloader unlock successful!");
        emit infoMessage(QStringLiteral("Bootloader unlocked successfully"));
        return true;
    }

    // Try alternative command format
    LOG_WARNING_CAT(TAG, "Primary unlock failed, trying alternative command...");
    resp = m_client->sendCommand(
        QStringLiteral("oem nv_unlock %1").arg(code));

    if (resp.isOkay()) {
        LOG_INFO_CAT(TAG, "Bootloader unlock successful (alt method)");
        emit infoMessage(QStringLiteral("Bootloader unlocked successfully"));
        return true;
    }

    LOG_ERROR_CAT(TAG, QStringLiteral("Bootloader unlock failed: %1").arg(resp.toString()));
    emit infoMessage(QStringLiteral("Bootloader unlock failed: %1")
                         .arg(QString::fromUtf8(resp.data)));
    return false;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

bool HuaweiHonorSupport::isHuaweiDevice() const
{
    // We don't store VID here directly; rely on the product string
    if (!m_client || !m_client->isConnected())
        return false;

    QString product = m_client->getVariable(QStringLiteral("manufacturer"));
    return product.contains(QStringLiteral("HUAWEI"), Qt::CaseInsensitive) ||
           product.contains(QStringLiteral("HONOR"), Qt::CaseInsensitive);
}

QString HuaweiHonorSupport::getUnlockToken()
{
    if (!m_client || !m_client->isConnected())
        return {};

    // Huawei: oem get-token or oem get_identifier_token
    FastbootResponse resp = sendHuaweiOem(QStringLiteral("get_identifier_token"));
    if (resp.isOkay() || resp.isInfo()) {
        // The token is usually returned across multiple INFO messages.
        // Our sendHuaweiOem collects them via the signal. The final data
        // may also contain part of it.
        QString token = QString::fromUtf8(resp.data).trimmed();
        LOG_INFO_CAT(TAG, QStringLiteral("Unlock token: %1").arg(token));
        return token;
    }

    LOG_ERROR_CAT(TAG, "Failed to retrieve unlock token");
    return {};
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

FastbootResponse HuaweiHonorSupport::sendHuaweiOem(const QString& subcmd)
{
    return m_client->sendCommand(QStringLiteral("oem %1").arg(subcmd));
}

} // namespace sakura
