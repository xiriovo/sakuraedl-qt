#include "fastboot_client.h"
#include "core/logger.h"

#include <QThread>

namespace sakura {

static constexpr const char* TAG = "FastbootClient";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FastbootClient::FastbootClient(UsbTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

bool FastbootClient::connect()
{
    if (!m_transport || !m_transport->isOpen()) {
        LOG_ERROR_CAT(TAG, "Transport not open");
        return false;
    }

    // Verify protocol by reading the Fastboot version variable
    QString version = getVariable(QStringLiteral("version"));
    if (version.isEmpty()) {
        LOG_ERROR_CAT(TAG, "Device did not respond to getvar:version");
        return false;
    }
    LOG_INFO_CAT(TAG, QStringLiteral("Fastboot version: %1").arg(version));

    // Query maximum download size
    QString maxDlStr = getVariable(QStringLiteral("max-download-size"));
    if (!maxDlStr.isEmpty()) {
        bool ok = false;
        uint32_t size = 0;
        if (maxDlStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            size = maxDlStr.toUInt(&ok, 16);
        else
            size = maxDlStr.toUInt(&ok, 10);
        if (ok && size > 0)
            m_maxDownloadSize = size;
    }
    LOG_INFO_CAT(TAG, QStringLiteral("Max download size: 0x%1 (%2 MiB)")
                          .arg(m_maxDownloadSize, 0, 16)
                          .arg(m_maxDownloadSize / (1024 * 1024)));

    m_connected = true;
    return true;
}

// ---------------------------------------------------------------------------
// Core commands
// ---------------------------------------------------------------------------

QString FastbootClient::getVariable(const QString& name)
{
    FastbootResponse resp = sendCommand(QStringLiteral("getvar:%1").arg(name));
    if (resp.isOkay())
        return QString::fromUtf8(resp.data).trimmed();
    return {};
}

bool FastbootClient::download(const QByteArray& data)
{
    if (data.isEmpty()) {
        LOG_ERROR_CAT(TAG, "download: empty data");
        return false;
    }

    // 1. Send download:<hex-size>
    QByteArray cmd = FastbootProtocol::buildDownloadCommand(
        static_cast<uint32_t>(data.size()));
    m_transport->write(cmd);

    FastbootResponse resp = readFinalResponse();
    if (!resp.isData()) {
        LOG_ERROR_CAT(TAG, QStringLiteral("download: expected DATA, got %1")
                               .arg(resp.toString()));
        return false;
    }

    // 2. Stream the payload with progress
    if (!sendData(data))
        return false;

    // 3. Read final OKAY
    resp = readFinalResponse();
    if (!resp.isOkay()) {
        LOG_ERROR_CAT(TAG, QStringLiteral("download: device rejected data – %1")
                               .arg(resp.toString()));
        return false;
    }
    return true;
}

bool FastbootClient::flash(const QString& partition, const QByteArray& data)
{
    LOG_INFO_CAT(TAG, QStringLiteral("Flashing %1 (%2 bytes)")
                          .arg(partition)
                          .arg(data.size()));

    if (!download(data))
        return false;

    FastbootResponse resp = sendCommand(QStringLiteral("flash:%1").arg(partition));
    if (!resp.isOkay()) {
        LOG_ERROR_CAT(TAG, QStringLiteral("flash %1 failed: %2")
                               .arg(partition, resp.toString()));
        return false;
    }

    LOG_INFO_CAT(TAG, QStringLiteral("Flash %1 complete").arg(partition));
    return true;
}

bool FastbootClient::erase(const QString& partition)
{
    LOG_INFO_CAT(TAG, QStringLiteral("Erasing %1").arg(partition));
    FastbootResponse resp = sendCommand(QStringLiteral("erase:%1").arg(partition));
    if (!resp.isOkay()) {
        LOG_ERROR_CAT(TAG, QStringLiteral("erase %1 failed: %2")
                               .arg(partition, resp.toString()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reboot commands
// ---------------------------------------------------------------------------

bool FastbootClient::reboot()
{
    FastbootResponse resp = sendCommand(QStringLiteral("reboot"));
    m_connected = false;
    return resp.isOkay();
}

bool FastbootClient::rebootBootloader()
{
    FastbootResponse resp = sendCommand(QStringLiteral("reboot-bootloader"));
    m_connected = false;
    return resp.isOkay();
}

bool FastbootClient::rebootRecovery()
{
    FastbootResponse resp = sendCommand(QStringLiteral("reboot-recovery"));
    m_connected = false;
    return resp.isOkay();
}

bool FastbootClient::rebootFastbootd()
{
    FastbootResponse resp = sendCommand(QStringLiteral("reboot-fastboot"));
    m_connected = false;
    return resp.isOkay();
}

// ---------------------------------------------------------------------------
// OEM commands
// ---------------------------------------------------------------------------

bool FastbootClient::oemUnlock()
{
    FastbootResponse resp = sendCommand(QStringLiteral("oem unlock"));
    return resp.isOkay();
}

bool FastbootClient::oemLock()
{
    FastbootResponse resp = sendCommand(QStringLiteral("oem lock"));
    return resp.isOkay();
}

bool FastbootClient::oemCommand(const QString& command)
{
    FastbootResponse resp = sendCommand(QStringLiteral("oem %1").arg(command));
    return resp.isOkay();
}

// ---------------------------------------------------------------------------
// Low-level I/O
// ---------------------------------------------------------------------------

FastbootResponse FastbootClient::sendCommand(const QString& command)
{
    LOG_DEBUG_CAT(TAG, QStringLiteral(">> %1").arg(command));
    QByteArray pkt = command.toUtf8();
    if (pkt.size() > FastbootProtocol::MAX_COMMAND_LENGTH) {
        LOG_WARNING_CAT(TAG, QStringLiteral("Command truncated: %1 -> %2 bytes")
                                 .arg(pkt.size()).arg(FastbootProtocol::MAX_COMMAND_LENGTH));
        pkt.truncate(FastbootProtocol::MAX_COMMAND_LENGTH);
    }
    m_transport->write(pkt);
    return readFinalResponse();
}

bool FastbootClient::sendData(const QByteArray& data)
{
    const int chunkSize = 512 * 1024; // 512 KiB USB transfer chunks
    qint64 total = data.size();
    qint64 sent  = 0;

    while (sent < total) {
        qint64 remaining = total - sent;
        int    toSend    = static_cast<int>(qMin<qint64>(remaining, chunkSize));
        QByteArray chunk = data.mid(static_cast<int>(sent), toSend);

        qint64 written = m_transport->write(chunk);
        if (written < 0) {
            LOG_ERROR_CAT(TAG, QStringLiteral("sendData: write failed at offset %1").arg(sent));
            return false;
        }
        if (written != toSend) {
            LOG_WARNING_CAT(TAG, QStringLiteral("sendData: partial write %1/%2 at offset %3")
                                     .arg(written).arg(toSend).arg(sent));
        }

        sent += written;
        reportProgress(sent, total);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Response reading
// ---------------------------------------------------------------------------

FastbootResponse FastbootClient::readResponse()
{
    // Fastboot responses can include long INFO messages; 64 KiB is sufficient
    QByteArray raw = m_transport->read(65536, m_responseTimeoutMs);
    return FastbootProtocol::parseResponse(raw);
}

FastbootResponse FastbootClient::readFinalResponse()
{
    // Keep reading until we get a non-INFO response
    constexpr int MAX_INFO_ITERATIONS = 256;
    for (int i = 0; i < MAX_INFO_ITERATIONS; ++i) {
        FastbootResponse resp = readResponse();
        if (resp.isInfo()) {
            QString msg = QString::fromUtf8(resp.data).trimmed();
            LOG_INFO_CAT(TAG, QStringLiteral("(info) %1").arg(msg));
            emit infoReceived(msg);
            continue;
        }
        if (resp.type == FastbootResponseType::Unknown && resp.data.isEmpty()) {
            LOG_ERROR_CAT(TAG, "readFinalResponse: timeout waiting for device response");
            return resp;
        }
        LOG_DEBUG_CAT(TAG, QStringLiteral("<< %1").arg(resp.toString()));
        return resp;
    }
    LOG_ERROR_CAT(TAG, "readFinalResponse: exceeded max INFO iterations");
    FastbootResponse timeout;
    timeout.type = FastbootResponseType::Fail;
    timeout.data = "Too many INFO responses";
    return timeout;
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

void FastbootClient::reportProgress(qint64 current, qint64 total)
{
    if (m_progressCb)
        m_progressCb(current, total);
    emit progressUpdated(current, total);
}

} // namespace sakura
