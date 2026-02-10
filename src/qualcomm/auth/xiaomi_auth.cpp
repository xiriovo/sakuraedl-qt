#include "xiaomi_auth.h"
#include "qualcomm/protocol/firehose_client.h"
#include "core/logger.h"

#include <QFile>
#include <QXmlStreamReader>

static const QString TAG = QStringLiteral("XiaomiAuth");

namespace sakura {

XiaomiAuth::XiaomiAuth() = default;

bool XiaomiAuth::loadAuthFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QString("Cannot open auth file: %1").arg(path));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    // Xiaomi auth files can be:
    // 1. Raw signature blob (256 bytes for RSA-2048)
    // 2. XML format with <auth> elements
    // 3. JSON token from MiFlash

    if (data.size() == AUTH_SIGNATURE_SIZE) {
        m_signature = data;
        LOG_INFO_CAT(TAG, "Loaded raw auth signature");
        return true;
    }

    // Try parsing as XML
    QXmlStreamReader reader(data);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("sig") ||
                reader.name() == QStringLiteral("signature")) {
                QString sigHex = reader.readElementText().trimmed();
                m_signature = QByteArray::fromHex(sigHex.toLatin1());
                LOG_INFO_CAT(TAG, QString("Loaded XML auth signature (%1 bytes)")
                                .arg(m_signature.size()));
                return true;
            } else if (reader.name() == QStringLiteral("programmer_sig")) {
                QString sigHex = reader.readElementText().trimmed();
                m_programmerSig = QByteArray::fromHex(sigHex.toLatin1());
            }
        }
    }

    if (!m_signature.isEmpty())
        return true;

    // Treat the whole file as a signature if it's a reasonable size
    if (data.size() <= 4096) {
        m_signature = data;
        LOG_INFO_CAT(TAG, "Loaded auth blob");
        return true;
    }

    LOG_ERROR_CAT(TAG, "Could not parse auth file format");
    return false;
}

void XiaomiAuth::setSignature(const QByteArray& sig)
{
    m_signature = sig;
}

void XiaomiAuth::setProgrammerSignature(const QByteArray& sig)
{
    m_programmerSig = sig;
}

bool XiaomiAuth::requiresAuth(const QString& chipName)
{
    // Most Xiaomi devices with SDM/SM chipsets from 2019+ require auth
    static const QStringList authChips = {
        "sm8150", "sm8250", "sm8350", "sm8450", "sm8550", "sm8650",
        "sm6150", "sm6250", "sm6350", "sm6375", "sm6450",
        "sm7150", "sm7250", "sm7325", "sm7350", "sm7450",
        "sdm845", "sdm855", "sdm865",
    };

    QString lower = chipName.toLower();
    for (const auto& chip : authChips) {
        if (lower.contains(chip))
            return true;
    }
    return false;
}

bool XiaomiAuth::authenticateAsync(FirehoseClient* client)
{
    LOG_INFO_CAT(TAG, "Starting Xiaomi authentication");

    if (m_signature.isEmpty()) {
        LOG_ERROR_CAT(TAG, "No auth signature loaded");
        return false;
    }

    // Step 1: Send programmer signature if available
    if (!m_programmerSig.isEmpty()) {
        LOG_INFO_CAT(TAG, "Sending programmer signature");
        if (!sendSignature(client, m_programmerSig)) {
            LOG_WARNING_CAT(TAG, "Programmer signature rejected");
        }
    }

    // Step 2: Send the main auth signature
    LOG_INFO_CAT(TAG, "Sending device auth signature");
    if (!sendSignature(client, m_signature)) {
        LOG_ERROR_CAT(TAG, "Auth signature rejected");
        return false;
    }

    LOG_INFO_CAT(TAG, "Xiaomi authentication successful");
    return true;
}

bool XiaomiAuth::sendSignature(FirehoseClient* client, const QByteArray& sig)
{
    QByteArray xml = buildAuthXml(sig);
    auto resp = client->sendRawXml(QString::fromUtf8(xml));
    return resp.success;
}

QByteArray XiaomiAuth::buildAuthXml(const QByteArray& signature)
{
    // Xiaomi Firehose auth format:
    // <?xml version="1.0" ?><data><sig size_in_bytes="256">HEX_SIGNATURE</sig></data>
    QString xml = QString(
        "<?xml version=\"1.0\" ?>"
        "<data><sig size_in_bytes=\"%1\">%2</sig></data>")
        .arg(signature.size())
        .arg(QString(signature.toHex()));

    return xml.toUtf8();
}

} // namespace sakura
