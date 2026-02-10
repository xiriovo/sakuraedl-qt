#include "vip_auth.h"
#include "qualcomm/protocol/firehose_client.h"
#include "core/logger.h"

#include <QFile>

static const QString TAG = QStringLiteral("VipAuth");

namespace sakura {

VipAuth::VipAuth() = default;

bool VipAuth::loadDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open digest file: %1").arg(path));
        return false;
    }
    m_digest = file.readAll();
    file.close();
    LOG_INFO_CAT(TAG, QString("Loaded digest: %1 bytes").arg(m_digest.size()));
    return !m_digest.isEmpty();
}

bool VipAuth::loadSignature(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open signature file: %1").arg(path));
        return false;
    }
    m_signature = file.readAll();
    file.close();
    LOG_INFO_CAT(TAG, QString("Loaded signature: %1 bytes").arg(m_signature.size()));
    return !m_signature.isEmpty();
}

void VipAuth::setDigest(const QByteArray& digest)
{
    m_digest = digest;
}

void VipAuth::setSignature(const QByteArray& signature)
{
    m_signature = signature;
}

bool VipAuth::authenticateAsync(FirehoseClient* client)
{
    LOG_INFO_CAT(TAG, "Starting VIP authentication");

    if (m_digest.isEmpty() || m_signature.isEmpty()) {
        LOG_ERROR_CAT(TAG, "Digest or signature not loaded");
        return false;
    }

    LOG_INFO_CAT(TAG, QString("Sending VIP digest (%1 bytes) + signature (%2 bytes)")
                    .arg(m_digest.size()).arg(m_signature.size()));

    // Step 1: Send digest as <sig> element
    QString digestXml = QString(
        "<?xml version=\"1.0\" ?>"
        "<data><sig size_in_bytes=\"%1\">%2</sig></data>")
        .arg(m_digest.size())
        .arg(QString(m_digest.toHex()));

    auto digestResp = client->sendRawXml(digestXml);
    if (!digestResp.success) {
        LOG_WARNING_CAT(TAG, "Digest send returned non-success, trying signature anyway");
    }

    // Step 2: Send signature
    QString signXml = QString(
        "<?xml version=\"1.0\" ?>"
        "<data><sig size_in_bytes=\"%1\">%2</sig></data>")
        .arg(m_signature.size())
        .arg(QString(m_signature.toHex()));

    auto signResp = client->sendRawXml(signXml);
    if (!signResp.success) {
        LOG_ERROR_CAT(TAG, "Signature rejected by device");
        return false;
    }

    LOG_INFO_CAT(TAG, "VIP authentication successful");
    return true;
}

} // namespace sakura
