#include "spreadtrum_service.h"
#include "spreadtrum/protocol/sprd_diag_client.h"
#include "spreadtrum/parsers/pac_parser.h"
#include "spreadtrum/database/sprd_fdl_database.h"
#include "transport/i_transport.h"
#include "core/logger.h"

namespace sakura {

static constexpr char LOG_TAG[] = "SPRD-SVC";

SpreadtrumService::SpreadtrumService(QObject* parent)
    : QObject(parent)
{
}

SpreadtrumService::~SpreadtrumService()
{
    disconnect();
}

// ── Connection ──────────────────────────────────────────────────────────────

bool SpreadtrumService::connectDevice(ITransport* transport)
{
    if (m_connected) {
        LOG_WARNING_CAT(LOG_TAG, "Already connected, disconnecting first");
        disconnect();
    }

    m_transport = transport;
    m_fdlClient = std::make_unique<FdlClient>(transport, this);

    connect(m_fdlClient.get(), &FdlClient::transferProgress,
            this, &SpreadtrumService::transferProgress);

    if (!performHandshake()) {
        emit operationCompleted(false, "FDL handshake failed");
        return false;
    }

    m_connected = true;
    emit stateChanged(1); // Connected
    return true;
}

void SpreadtrumService::disconnect()
{
    m_fdlClient.reset();
    m_diagClient.reset();
    m_pacParser.reset();
    m_transport = nullptr;
    m_connected = false;

    emit stateChanged(0); // Disconnected
}

// ── FDL download ────────────────────────────────────────────────────────────

bool SpreadtrumService::loadFdl1(const QByteArray& data, uint32_t addr)
{
    if (!m_connected || !m_fdlClient) {
        emit operationCompleted(false, "Not connected");
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, "Loading FDL1...");

    if (!m_fdlClient->downloadFdl(data, addr, FdlStage::FDL1)) {
        emit operationCompleted(false, "Failed to download FDL1");
        return false;
    }

    if (!m_fdlClient->execData(addr)) {
        emit operationCompleted(false, "Failed to execute FDL1");
        return false;
    }

    emit stateChanged(2); // Fdl1Loaded
    LOG_INFO_CAT(LOG_TAG, "FDL1 loaded and executing");
    return true;
}

bool SpreadtrumService::loadFdl2(const QByteArray& data, uint32_t addr)
{
    if (!m_connected || !m_fdlClient) {
        emit operationCompleted(false, "Not connected");
        return false;
    }

    if (m_fdlClient->currentStage() != FdlStage::FDL1) {
        LOG_WARNING_CAT(LOG_TAG, "FDL1 not loaded — attempting FDL2 download anyway");
    }

    LOG_INFO_CAT(LOG_TAG, "Loading FDL2...");

    // Disable transcoding before binary transfer
    m_fdlClient->disableTranscode();

    if (!m_fdlClient->downloadFdl(data, addr, FdlStage::FDL2)) {
        emit operationCompleted(false, "Failed to download FDL2");
        return false;
    }

    if (!m_fdlClient->execData(addr)) {
        emit operationCompleted(false, "Failed to execute FDL2");
        return false;
    }

    emit stateChanged(3); // Fdl2Loaded

    // Re-handshake with FDL2
    if (!m_fdlClient->connect()) {
        LOG_WARNING_CAT(LOG_TAG, "FDL2 connect handshake failed");
    }

    emit stateChanged(4); // Ready
    LOG_INFO_CAT(LOG_TAG, "FDL2 loaded — device ready");
    return true;
}

bool SpreadtrumService::loadFdl1FromDatabase(uint16_t chipId)
{
    const SprdFdlDatabase& db = SprdFdlDatabase::instance();
    auto fdlInfo = db.fdlForChip(chipId);

    if (!fdlInfo.isValid()) {
        emit operationCompleted(false, QString("No FDL1 in database for chip 0x%1")
                                           .arg(chipId, 4, 16, QChar('0')));
        return false;
    }

    // FDL1 is typically extracted from PAC firmware or provided separately.
    // If we have a PAC loaded, look for FDL1 in the PAC file entries.
    if (m_pacParser) {
        for (const auto& entry : m_pacParser->getFiles()) {
            if (entry.partitionName.toLower() == "fdl1" || entry.fileName.toLower().contains("fdl1")) {
                QByteArray fdl1Data = m_pacParser->readFileData(entry);
                if (!fdl1Data.isEmpty()) {
                    uint32_t addr = fdlInfo.fdl1LoadAddr;
                    LOG_INFO_CAT(LOG_TAG, QString("Loading FDL1 from PAC (%1 bytes) → 0x%2")
                                              .arg(fdl1Data.size()).arg(addr, 8, 16, QChar('0')));
                    return m_fdlClient->downloadFdl(fdl1Data, addr, FdlStage::FDL1);
                }
            }
        }
    }

    LOG_WARNING_CAT(LOG_TAG, "FDL1 not available in PAC or database");
    return false;
}

bool SpreadtrumService::loadFdl2FromDatabase(uint16_t chipId)
{
    const SprdFdlDatabase& db = SprdFdlDatabase::instance();
    auto fdlInfo = db.fdlForChip(chipId);

    if (m_pacParser) {
        for (const auto& entry : m_pacParser->getFiles()) {
            if (entry.partitionName.toLower() == "fdl2" || entry.fileName.toLower().contains("fdl2")) {
                QByteArray fdl2Data = m_pacParser->readFileData(entry);
                if (!fdl2Data.isEmpty()) {
                    uint32_t addr = fdlInfo.isValid() ? fdlInfo.fdl2LoadAddr : 0x9EFFFE00;
                    LOG_INFO_CAT(LOG_TAG, QString("Loading FDL2 from PAC (%1 bytes) → 0x%2")
                                              .arg(fdl2Data.size()).arg(addr, 8, 16, QChar('0')));
                    return m_fdlClient->downloadFdl(fdl2Data, addr, FdlStage::FDL2);
                }
            }
        }
    }

    Q_UNUSED(chipId)
    LOG_WARNING_CAT(LOG_TAG, "FDL2 not available in PAC or database");
    return false;
}

// ── PAC firmware flash ──────────────────────────────────────────────────────

bool SpreadtrumService::loadPacFile(const QString& path)
{
    LOG_INFO_CAT(LOG_TAG, QString("Loading PAC file: %1").arg(path));

    m_pacParser = std::make_unique<PacParser>();
    if (!m_pacParser->parse(path)) {
        emit operationCompleted(false, "Failed to parse PAC file: " + m_pacParser->errorString());
        m_pacParser.reset();
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, QString("PAC loaded: %1 files").arg(m_pacParser->fileCount()));
    return true;
}

bool SpreadtrumService::flashPac()
{
    if (!m_connected || !m_fdlClient || !m_pacParser) {
        emit operationCompleted(false, "Not ready to flash PAC");
        return false;
    }

    if (m_fdlClient->currentStage() != FdlStage::FDL2) {
        emit operationCompleted(false, "FDL2 must be loaded before flashing");
        return false;
    }

    LOG_INFO_CAT(LOG_TAG, "Starting PAC flash...");

    // Flash each partition file from the PAC
    auto files = m_pacParser->getFiles();
    int total = files.size();
    int current = 0;

    for (const auto& file : files) {
        ++current;
        emit logMessage(QString("Flashing %1 (%2/%3)").arg(file.partitionName)
                            .arg(current).arg(total));

        QByteArray data = m_pacParser->readFileData(file);
        if (data.isEmpty()) {
            LOG_WARNING_CAT(LOG_TAG, QString("Skipping empty file: %1").arg(file.fileName));
            continue;
        }

        if (!writePartition(file.partitionName, data)) {
            emit operationCompleted(false, QString("Failed to flash %1").arg(file.partitionName));
            return false;
        }
    }

    emit operationCompleted(true, "PAC flash completed successfully");
    return true;
}

// ── Partition operations ────────────────────────────────────────────────────

QList<PartitionInfo> SpreadtrumService::readPartitions()
{
    if (!m_fdlClient) return {};

    auto partitions = m_fdlClient->readPartitions();
    emit partitionsReady(partitions);
    return partitions;
}

bool SpreadtrumService::writePartition(const QString& name, const QByteArray& data)
{
    if (!m_fdlClient) return false;
    return m_fdlClient->writePartition(name, data);
}

QByteArray SpreadtrumService::readPartition(const QString& name, qint64 offset, qint64 length)
{
    if (!m_fdlClient) return {};
    return m_fdlClient->readPartition(name, offset, length);
}

bool SpreadtrumService::erasePartition(const QString& name)
{
    if (!m_fdlClient) return false;
    return m_fdlClient->erasePartition(name);
}

// ── Diag operations ─────────────────────────────────────────────────────────

QByteArray SpreadtrumService::readImei()
{
    if (!m_fdlClient) return {};
    return m_fdlClient->readImei();
}

bool SpreadtrumService::writeImei(const QByteArray& imei1, const QByteArray& imei2)
{
    if (!m_fdlClient) return false;
    return m_fdlClient->writeImei(imei1, imei2);
}

// ── Device info ─────────────────────────────────────────────────────────────

QString SpreadtrumService::getVersion()
{
    if (!m_fdlClient) return {};
    return m_fdlClient->getVersion();
}

FdlStage SpreadtrumService::currentStage() const
{
    if (!m_fdlClient) return FdlStage::None;
    return m_fdlClient->currentStage();
}

// ── Control ─────────────────────────────────────────────────────────────────

bool SpreadtrumService::reboot()
{
    if (!m_fdlClient) return false;
    return m_fdlClient->normalReset();
}

bool SpreadtrumService::powerOff()
{
    if (!m_fdlClient) return false;
    return m_fdlClient->powerOff();
}

// ── Private ─────────────────────────────────────────────────────────────────

bool SpreadtrumService::performHandshake()
{
    LOG_INFO_CAT(LOG_TAG, "Performing FDL handshake...");

    if (!m_fdlClient->handshake())
        return false;

    return m_fdlClient->connect();
}

bool SpreadtrumService::enterFdl2()
{
    if (m_fdlClient->currentStage() == FdlStage::FDL2)
        return true; // Already in FDL2

    if (m_fdlClient->currentStage() != FdlStage::FDL1) {
        LOG_ERROR_CAT(LOG_TAG, "Must be in FDL1 stage to transition to FDL2");
        return false;
    }

    // In FDL1 mode: disable transcode, then download and execute FDL2
    LOG_INFO_CAT(LOG_TAG, "Transitioning FDL1 → FDL2...");

    // Step 1: Negotiate baud rate (FDL1 may support higher rate)
    m_fdlClient->changeBaudRate(921600);

    // Step 2: Load FDL2 from PAC or database
    // chipId is stored in m_deviceInfo from handshake
    uint16_t chipId = 0; // Will be populated from device info
    if (!loadFdl2FromDatabase(chipId)) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to load FDL2");
        return false;
    }

    // Step 3: Execute FDL2 at the FDL2 load address
    const SprdFdlDatabase& db = SprdFdlDatabase::instance();
    auto fdlInfo = db.fdlForChip(chipId);
    uint32_t execAddr = fdlInfo.isValid() ? fdlInfo.fdl2EntryAddr : 0x9EFFFE00;
    if (!m_fdlClient->execData(execAddr)) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to execute FDL2");
        return false;
    }

    // Step 4: Disable transcoding in FDL2 mode for faster transfers
    m_fdlClient->disableTranscode();

    LOG_INFO_CAT(LOG_TAG, "FDL2 is now active");
    return m_fdlClient->currentStage() == FdlStage::FDL2;
}

} // namespace sakura
