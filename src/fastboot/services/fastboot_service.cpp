#include "fastboot_service.h"
#include "fastboot/parsers/sparse_image.h"
#include "core/logger.h"

#include <QFile>

namespace sakura {

static constexpr const char* TAG = "FastbootService";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FastbootService::FastbootService(QObject* parent)
    : QObject(parent)
{
}

FastbootService::~FastbootService()
{
    disconnect();
}

// ---------------------------------------------------------------------------
// Device enumeration & selection
// ---------------------------------------------------------------------------

QStringList FastbootService::detectDevices()
{
    QStringList result;
    auto vids = FastbootProtocol::knownVids();
    for (uint16_t vid : vids) {
        auto devices = UsbTransport::enumerateDevices(vid, FastbootProtocol::PID_FASTBOOT);
        for (const auto& dev : devices) {
            if (!dev.serial.isEmpty())
                result.append(dev.serial);
        }
    }
    return result;
}

bool FastbootService::selectDevice(const QString& serial)
{
    // Disconnect previous if any
    disconnect();

    // Find the device
    UsbDeviceInfo target;
    bool found = false;
    auto vids = FastbootProtocol::knownVids();

    for (uint16_t vid : vids) {
        auto devices = UsbTransport::enumerateDevices(vid, FastbootProtocol::PID_FASTBOOT);
        for (const auto& dev : devices) {
            if (serial.isEmpty() || dev.serial == serial) {
                target = dev;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        LOG_ERROR_CAT(TAG, QStringLiteral("No Fastboot device found%1")
                               .arg(serial.isEmpty() ? QString()
                                                     : QStringLiteral(" (serial=%1)").arg(serial)));
        return false;
    }

    // Open transport
    m_transport = std::make_unique<UsbTransport>(target.vid, target.pid);
    if (!m_transport->open()) {
        LOG_ERROR_CAT(TAG, "Failed to open USB transport");
        m_transport.reset();
        return false;
    }

    // Create client
    m_client = std::make_unique<FastbootClient>(m_transport.get(), this);
    m_client->setProgressCallback([this](qint64 cur, qint64 tot) {
        reportProgress(cur, tot);
    });
    QObject::connect(m_client.get(), &FastbootClient::infoReceived,
                     this, &FastbootService::operationInfo);

    if (!m_client->connect()) {
        LOG_ERROR_CAT(TAG, "Fastboot handshake failed");
        m_client.reset();
        m_transport.reset();
        return false;
    }

    LOG_INFO_CAT(TAG, QStringLiteral("Connected to %1 (VID=%2 PID=%3)")
                          .arg(target.serial)
                          .arg(target.vid, 4, 16, QLatin1Char('0'))
                          .arg(target.pid, 4, 16, QLatin1Char('0')));

    emit deviceConnected(target.serial);
    return true;
}

void FastbootService::disconnect()
{
    if (m_client)
        m_client.reset();
    if (m_transport) {
        if (m_transport->isOpen())
            m_transport->close();
        m_transport.reset();
    }
    m_deviceInfo = {};
    emit deviceDisconnected();
}

bool FastbootService::isConnected() const
{
    return m_client && m_client->isConnected();
}

// ---------------------------------------------------------------------------
// Device info
// ---------------------------------------------------------------------------

FastbootDeviceInfo FastbootService::refreshDeviceInfo()
{
    FastbootDeviceInfo info;
    if (!isConnected())
        return info;

    info.serialNumber      = m_client->getVariable(QStringLiteral("serialno"));
    info.product           = m_client->getVariable(QStringLiteral("product"));
    info.bootloaderVersion = m_client->getVariable(QStringLiteral("version-bootloader"));
    info.baseband          = m_client->getVariable(QStringLiteral("version-baseband"));
    info.hardwareRevision  = m_client->getVariable(QStringLiteral("hw-revision"));
    info.secureState       = m_client->getVariable(QStringLiteral("secure"));

    QString unlocked = m_client->getVariable(QStringLiteral("unlocked"));
    info.isUnlocked = (unlocked.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);

    // Try to read the partition list (some bootloaders support "getvar:all"
    // which returns "partition-type:<name>:..." lines via INFO messages).
    // For now, store a few common partition sizes.
    static const QStringList commonPartitions = {
        QStringLiteral("boot"), QStringLiteral("recovery"),
        QStringLiteral("system"), QStringLiteral("vendor"),
        QStringLiteral("userdata"), QStringLiteral("dtbo"),
        QStringLiteral("vbmeta"), QStringLiteral("super"),
    };

    for (const QString& p : commonPartitions) {
        QString sizeStr = m_client->getVariable(QStringLiteral("partition-size:%1").arg(p));
        if (!sizeStr.isEmpty())
            info.partitions.insert(p, sizeStr);
    }

    m_deviceInfo = info;
    LOG_INFO_CAT(TAG, QStringLiteral("Device: %1 / %2 / BL=%3")
                          .arg(info.serialNumber, info.product, info.bootloaderVersion));
    return info;
}

// ---------------------------------------------------------------------------
// Flash / erase
// ---------------------------------------------------------------------------

bool FastbootService::flashPartition(const QString& partition, const QString& filePath)
{
    QByteArray data = readImageFile(filePath);
    if (data.isEmpty()) {
        emit operationFinished(false, QStringLiteral("Failed to read %1").arg(filePath));
        return false;
    }
    return flashPartition(partition, data);
}

bool FastbootService::flashPartition(const QString& partition, const QByteArray& data)
{
    if (!isConnected()) {
        emit operationFinished(false, QStringLiteral("Not connected"));
        return false;
    }

    // If the image is sparse and larger than max-download-size, re-sparse
    // into chunks that fit.
    uint32_t maxDl = m_client->maxDownloadSize();
    if (SparseImage::isSparse(data) &&
        static_cast<uint32_t>(data.size()) > maxDl) {

        auto chunks = SparseImage::splitForTransfer(data, maxDl);
        LOG_INFO_CAT(TAG, QStringLiteral("Sparse image split into %1 chunk(s)")
                              .arg(chunks.size()));

        for (size_t i = 0; i < chunks.size(); ++i) {
            LOG_INFO_CAT(TAG, QStringLiteral("Flashing sparse chunk %1/%2")
                                  .arg(i + 1).arg(chunks.size()));
            if (!m_client->flash(partition, chunks[i])) {
                emit operationFinished(false, QStringLiteral("Sparse flash failed at chunk %1").arg(i + 1));
                return false;
            }
        }
    } else {
        if (!m_client->flash(partition, data)) {
            emit operationFinished(false, QStringLiteral("Flash %1 failed").arg(partition));
            return false;
        }
    }

    emit operationFinished(true, QStringLiteral("Flash %1 complete").arg(partition));
    return true;
}

bool FastbootService::erasePartition(const QString& partition)
{
    if (!isConnected()) {
        emit operationFinished(false, QStringLiteral("Not connected"));
        return false;
    }
    bool ok = m_client->erase(partition);
    emit operationFinished(ok, ok ? QStringLiteral("Erase %1 complete").arg(partition)
                                  : QStringLiteral("Erase %1 failed").arg(partition));
    return ok;
}

bool FastbootService::flashScript(const std::vector<FlashTask>& tasks)
{
    if (!isConnected()) {
        emit operationFinished(false, QStringLiteral("Not connected"));
        return false;
    }

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        LOG_INFO_CAT(TAG, QStringLiteral("[%1/%2] %3 -> %4")
                              .arg(i + 1).arg(tasks.size())
                              .arg(task.filePath, task.partition));

        if (task.erase) {
            if (!erasePartition(task.partition))
                return false;
        }
        if (!flashPartition(task.partition, task.filePath))
            return false;
    }

    emit operationFinished(true, QStringLiteral("Flash script complete (%1 tasks)")
                                      .arg(tasks.size()));
    return true;
}

// ---------------------------------------------------------------------------
// Reboot / OEM wrappers
// ---------------------------------------------------------------------------

bool FastbootService::reboot()             { return isConnected() && m_client->reboot(); }
bool FastbootService::rebootBootloader()   { return isConnected() && m_client->rebootBootloader(); }
bool FastbootService::rebootRecovery()     { return isConnected() && m_client->rebootRecovery(); }
bool FastbootService::oemUnlock()          { return isConnected() && m_client->oemUnlock(); }
bool FastbootService::oemLock()            { return isConnected() && m_client->oemLock(); }

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void FastbootService::reportProgress(qint64 current, qint64 total)
{
    if (m_progressCb)
        m_progressCb(current, total);
    emit operationProgress(current, total);
}

QByteArray FastbootService::readImageFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(TAG, QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
        return {};
    }
    return file.readAll();
}

} // namespace sakura
