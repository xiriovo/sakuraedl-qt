#include "qualcomm_service.h"
#include "qualcomm/auth/i_auth_strategy.h"
#include "transport/i_transport.h"
#include "core/logger.h"

static const QString TAG = QStringLiteral("QualcommService");

namespace sakura {

QualcommService::QualcommService(QObject* parent)
    : QObject(parent)
{
}

QualcommService::~QualcommService()
{
    disconnect();
}

void QualcommService::setState(DeviceState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(static_cast<int>(state));
    }
}

// ─── Connection ──────────────────────────────────────────────────────

bool QualcommService::connectDevice(ITransport* transport)
{
    if (!transport || !transport->isOpen()) {
        emit errorOccurred("Transport not open");
        return false;
    }

    m_transport = transport;
    LOG_INFO_CAT(TAG, QString("Connecting via %1").arg(transport->description()));

    // Create Sahara client and attempt handshake
    m_sahara = std::make_unique<SaharaClient>(m_transport, this);

    QObject::connect(m_sahara.get(), &SaharaClient::uploadProgress,
                     this, &QualcommService::transferProgress);
    QObject::connect(m_sahara.get(), &SaharaClient::statusMessage,
                     this, &QualcommService::statusMessage);

    if (!m_sahara->handshakeAsync()) {
        LOG_ERROR_CAT(TAG, "Sahara handshake failed");
        setState(DeviceState::Error);
        emit errorOccurred("Sahara handshake failed");
        return false;
    }

    setState(DeviceState::SaharaMode);

    // Device info was already read during handshake (Command mode)
    m_deviceInfo = m_sahara->getDeviceInfo();

    QVariantMap infoMap;
    infoMap["saharaVersion"] = m_deviceInfo.saharaVersion;
    infoMap["msmId"] = QString("0x%1").arg(m_deviceInfo.msmId, 8, 16, QChar('0'));
    infoMap["serial"] = m_deviceInfo.serialHex;
    infoMap["serialDec"] = m_deviceInfo.serial;
    infoMap["chipName"] = m_deviceInfo.chipName;
    infoMap["oemId"] = QString("0x%1").arg(m_deviceInfo.oemId, 4, 16, QChar('0'));
    infoMap["modelId"] = QString("0x%1").arg(m_deviceInfo.modelId, 4, 16, QChar('0'));
    infoMap["pkHash"] = m_deviceInfo.pkHashHex;
    infoMap["hwId"] = m_deviceInfo.hwIdHex;
    infoMap["sblVersion"] = QString("0x%1").arg(m_deviceInfo.sblVersion, 8, 16, QChar('0'));
    infoMap["chipInfoRead"] = m_deviceInfo.chipInfoRead;
    emit deviceInfoReady(infoMap);

    emit statusMessage("Device connected in Sahara mode");
    return true;
}

bool QualcommService::connectFirehoseDirect(ITransport* transport)
{
    if (!transport || !transport->isOpen()) {
        emit errorOccurred("Transport not open");
        return false;
    }

    m_transport = transport;
    LOG_INFO_CAT(TAG, QString("Direct Firehose connect via %1 (skip Sahara)").arg(transport->description()));

    // Skip Sahara entirely — go straight to Firehose
    setState(DeviceState::FirehoseMode);
    return enterFirehoseMode();
}

void QualcommService::disconnect()
{
    m_firehose.reset();
    m_sahara.reset();
    m_transport = nullptr;
    m_loaderData.clear();
    setState(DeviceState::Disconnected);
}

// ─── Loader upload ───────────────────────────────────────────────────

bool QualcommService::uploadLoader(const QByteArray& loaderData)
{
    if (m_state != DeviceState::SaharaMode || !m_sahara) {
        emit errorOccurred("Not in Sahara mode");
        return false;
    }

    LOG_INFO_CAT(TAG, QString("Uploading loader (%1 bytes)").arg(loaderData.size()));
    m_loaderData = loaderData;

    if (!m_sahara->uploadLoader(loaderData)) {
        LOG_ERROR_CAT(TAG, "Loader upload failed");
        emit errorOccurred("Loader upload failed");
        return false;
    }

    // NOTE: Do NOT enter Firehose mode here.
    // The caller must close/reopen the serial port first (port cycling),
    // then call connectFirehoseDirect() to configure Firehose on the new transport.
    LOG_INFO_CAT(TAG, "Loader uploaded OK — awaiting port cycling before Firehose");
    return true;
}

bool QualcommService::enterFirehoseMode()
{
    LOG_INFO_CAT(TAG, "Entering Firehose mode");

    m_firehose = std::make_unique<FirehoseClient>(m_transport, this);

    QObject::connect(m_firehose.get(), &FirehoseClient::transferProgress,
                     this, &QualcommService::transferProgress);
    QObject::connect(m_firehose.get(), &FirehoseClient::logMessage,
                     this, [this](const QString& msg) {
        LOG_DEBUG_CAT(TAG, QString("[Firehose] %1").arg(msg));
    });
    QObject::connect(m_firehose.get(), &FirehoseClient::statusMessage,
                     this, &QualcommService::statusMessage);

    // Configure Firehose
    if (!m_firehose->configure(m_storageType, m_maxPayloadSize)) {
        LOG_ERROR_CAT(TAG, "Firehose configure failed");
        setState(DeviceState::Error);
        emit errorOccurred("Firehose configuration failed");
        return false;
    }

    setState(DeviceState::FirehoseMode);

    // Authenticate if strategy is set
    if (m_authStrategy) {
        if (!authenticate()) {
            LOG_WARNING_CAT(TAG, "Authentication failed — some operations may be restricted");
        }
    }

    setState(DeviceState::Ready);
    emit statusMessage("Firehose mode ready");
    return true;
}

// ─── Authentication ──────────────────────────────────────────────────

void QualcommService::setAuthStrategy(std::shared_ptr<IAuthStrategy> auth)
{
    m_authStrategy = std::move(auth);
}

bool QualcommService::authenticate()
{
    if (!m_authStrategy || !m_firehose) {
        return false;
    }

    LOG_INFO_CAT(TAG, QString("Authenticating with %1 strategy")
                    .arg(m_authStrategy->name()));

    return m_authStrategy->authenticateAsync(m_firehose.get());
}

// ─── Partition operations ────────────────────────────────────────────

QList<PartitionInfo> QualcommService::readPartitions(uint32_t lun)
{
    if (m_state < DeviceState::FirehoseMode || !m_firehose) {
        emit errorOccurred("Not in Firehose mode");
        return {};
    }

    auto partitions = m_firehose->readGptPartitions(lun);
    emit partitionsReady(partitions);
    return partitions;
}

QByteArray QualcommService::readPartition(const QString& name, uint32_t lun,
                                           ProgressCallback progress)
{
    if (!m_firehose) {
        emit errorOccurred("Not connected");
        return {};
    }

    return m_firehose->readPartition(name, lun, progress);
}

bool QualcommService::writePartition(const QString& name, const QByteArray& data,
                                      uint32_t lun, ProgressCallback progress)
{
    if (!m_firehose) {
        emit errorOccurred("Not connected");
        return false;
    }

    return m_firehose->writePartition(name, data, lun, progress);
}

bool QualcommService::erasePartition(const QString& name, uint32_t lun)
{
    if (!m_firehose) {
        emit errorOccurred("Not connected");
        return false;
    }

    return m_firehose->erasePartition(name, lun);
}

// ─── Device control ──────────────────────────────────────────────────

bool QualcommService::reboot()
{
    if (m_firehose) {
        return m_firehose->reset();
    }
    if (m_sahara) {
        return m_sahara->sendReset();
    }
    return false;
}

bool QualcommService::powerOff()
{
    if (!m_firehose) return false;
    return m_firehose->powerOff();
}

bool QualcommService::setActiveSlot(const QString& slot)
{
    if (!m_firehose) return false;
    return m_firehose->setActiveSlot(slot);
}

// ─── Configuration ───────────────────────────────────────────────────

void QualcommService::setStorageType(FirehoseStorageType type)
{
    m_storageType = type;
}

void QualcommService::setMaxPayloadSize(uint32_t size)
{
    m_maxPayloadSize = size;
}

} // namespace sakura
