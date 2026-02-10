#include "qualcomm_controller.h"
#include "qualcomm/services/qualcomm_service.h"
#include "qualcomm/auth/oneplus_auth.h"
#include "qualcomm/auth/xiaomi_auth.h"
#include "qualcomm/auth/vip_auth.h"
#include "transport/serial_transport.h"
#include "transport/port_detector.h"
#include "transport/i_transport.h"
#include "core/logger.h"
#include "common/gpt_parser.h"
#include "common/partition_info.h"
#include "qualcomm/parsers/rawprogram_parser.h"
#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QThread>
#include <QSet>
#include <QDateTime>
#include <QTimerEvent>
#include <cstring>

#ifdef _WIN32
#include "transport/win32_serial_transport.h"
#endif

namespace sakura {

static QString fmtSize(uint64_t b) {
    if(b>=(1ULL<<30)) return QString("%1 GB").arg(b/double(1ULL<<30),0,'f',2);
    if(b>=(1ULL<<20)) return QString("%1 MB").arg(b/double(1ULL<<20),0,'f',1);
    if(b>=(1ULL<<10)) return QString("%1 KB").arg(b/double(1ULL<<10),0,'f',0);
    return QString("%1 B").arg(b);
}

QualcommController::QualcommController(QObject* parent)
    : QObject(parent)
    , m_service(std::make_unique<QualcommService>())
{
    // Wire service signals
    QObject::connect(m_service.get(), &QualcommService::transferProgress,
                     this, [this](qint64 c, qint64 t) {
        updateProgress(c, t, m_progressText);
    });
    QObject::connect(m_service.get(), &QualcommService::statusMessage,
                     this, [this](const QString& msg) { addLog(msg); });
    QObject::connect(m_service.get(), &QualcommService::errorOccurred,
                     this, [this](const QString& msg) { addLogErr(msg); });
}

QualcommController::~QualcommController() = default;

// ═══════════════════════════════════════════════════════════════════════════
// CONNECTION + AUTO-DETECT
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::startAutoDetect()
{
    if(m_watching) return;
    m_watching = true;
    m_detectStatus = L("正在扫描 EDL 9008 设备...", "Scanning for EDL 9008 devices...");
    addLog(m_detectStatus);
    setConnectionState(Connecting);

    if(m_watchTimerId == 0)
        m_watchTimerId = startTimer(500);
    emit connectionStateChanged();
    emit readinessChanged();
}

void QualcommController::stopAutoDetect()
{
    if(!m_watching) return;
    m_watching = false;
    if(m_watchTimerId) { killTimer(m_watchTimerId); m_watchTimerId = 0; }
    emit connectionStateChanged();
    emit readinessChanged();
}

void QualcommController::connectDevice(const QString& port)
{
    if(m_busy) return;
    stopAutoDetect(); // stop scanning when connecting

    QString targetPort = port;
    if(targetPort.isEmpty()) {
        auto ports = detectPorts();
        if(ports.isEmpty()) {
            addLog(L("未找到 EDL 设备，启动自动检测...", "No EDL device found, starting auto-detect..."));
            startAutoDetect();
            return;
        }
        targetPort = ports.first();
    }

    setBusy(true);
    m_portName = targetPort; emit portChanged();
    setConnectionState(Connecting);
    addLog(L("正在连接 ", "Connecting ") + targetPort
           + QString(" (auth=%1, storage=%2)").arg(m_authMode, m_storageType));

    bool skipSahara = m_skipSahara;
    (void)QtConcurrent::run([this, skipSahara](){
        // Configure storage type on service
        if(m_storageType == "emmc")
            m_service->setStorageType(FirehoseStorageType::eMMC);
        else
            m_service->setStorageType(FirehoseStorageType::UFS);

        if(skipSahara) {
            // ═══ SKIP SAHARA — Direct Firehose connection ═══
            QMetaObject::invokeMethod(this, [this](){
                addLog(L("跳过 Sahara，直接连接 Firehose...", "Skipping Sahara, direct Firehose connect..."));
                setConnectionState(FirehoseMode);
            }, Qt::QueuedConnection);

#ifdef _WIN32
            auto transport = std::make_unique<Win32SerialTransport>(m_portName, 921600);
#else
            auto transport = std::make_unique<SerialTransport>(m_portName, 921600);
#endif
            if(!transport->open()) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("串口打开失败: ", "Serial port open failed: ") + m_portName);
                    cancelPendingOp(L("串口打开失败", "Serial port open failed"));
                    setConnectionState(Disconnected); setBusy(false);
                }, Qt::QueuedConnection);
                return;
            }

            // Discard stale data for Firehose direct mode (skip Sahara)
            transport->discardInput();

            // Transfer ownership to controller so transport outlives the lambda
            m_ownedTransport = std::move(transport);

            bool ok = m_service->connectFirehoseDirect(m_ownedTransport.get());
            if(!ok) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("Firehose 直连失败", "Direct Firehose connect failed"));
                    cancelPendingOp(L("Firehose 连接失败", "Firehose connect failed"));
                    setConnectionState(Error); setBusy(false);
                    m_ownedTransport.reset();
                }, Qt::QueuedConnection);
                return;
            }

            // Auto-execute authentication if configured
            if(m_authMode != "none") {
                setupAuthStrategy();
                bool authOk = m_service->authenticate();
                QString authMode = m_authMode;
                QMetaObject::invokeMethod(this, [this, authOk, authMode](){
                    if(authOk)
                        addLogOk(L("验证成功: ", "Auth OK: ") + authMode);
                    else
                        addLogFail(L("验证失败: ", "Auth failed: ") + authMode);
                }, Qt::QueuedConnection);
            }

            QMetaObject::invokeMethod(this, [this](){
                setConnectionState(Ready);
                setBusy(false);
                addLogOk(L("Firehose 直连成功 → ", "Firehose direct OK → ") + m_storageType);
                addLogOk(L("已连接 ", "Connected to ") + m_portName);
                emit operationCompleted(true, L("已连接 ", "Connected to ") + m_portName);

                // Execute pending operation if any
                executePendingOp();
            }, Qt::QueuedConnection);

        } else {
            // ═══ NORMAL FLOW — Sahara handshake → upload loader → Firehose ═══
            // Per edl2: Qualcomm EDL 9008 uses 921600 baud
#ifdef _WIN32
            auto transport = std::make_unique<Win32SerialTransport>(m_portName, 921600);
#else
            auto transport = std::make_unique<SerialTransport>(m_portName, 921600);
#endif
            if(!transport->open()) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("串口打开失败: ", "Serial port open failed: ") + m_portName);
                    cancelPendingOp(L("串口打开失败", "Serial port open failed"));
                    setConnectionState(Disconnected); setBusy(false);
                }, Qt::QueuedConnection);
                return;
            }

            // Transfer ownership to controller so transport outlives the lambda
            m_ownedTransport = std::move(transport);

            // Sahara handshake
            bool ok = m_service->connectDevice(m_ownedTransport.get());
            if(!ok) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("Sahara 握手失败", "Sahara handshake failed"));
                    cancelPendingOp(L("Sahara 握手失败", "Sahara handshake failed"));
                    setConnectionState(Error); setBusy(false);
                    m_ownedTransport.reset();
                }, Qt::QueuedConnection);
                return;
            }

            // Fetch chip info read during handshake
            auto chipInfo = m_service->deviceInfo();
            QMetaObject::invokeMethod(this, [this, chipInfo](){
                addLogOk(L("Sahara 握手成功", "Sahara handshake OK"));
                addLog(QString("- Sahara version  : %1").arg(chipInfo.saharaVersion));
                if (chipInfo.chipInfoRead) {
                    addLog(QString("- Chip Serial Number : %1").arg(chipInfo.serialHex));
                    if (!chipInfo.pkHashHex.isEmpty())
                        addLog(QString("- OEM PKHASH : %1").arg(chipInfo.pkHashHex));
                    addLog(QString("- MSM HWID : 0x%1 | model_id:0x%2 | oem_id:0x%3")
                        .arg(chipInfo.msmId, 0, 16)
                        .arg(chipInfo.modelId, 4, 16, QChar('0'))
                        .arg(chipInfo.oemId, 4, 16, QChar('0')));
                    if (!chipInfo.chipName.isEmpty())
                        addLog(QString("- CHIP : %1").arg(chipInfo.chipName));
                    if (!chipInfo.hwIdHex.isEmpty())
                        addLog(QString("- HW_ID : %1").arg(chipInfo.hwIdHex));
                    if (chipInfo.sblVersion != 0)
                        addLog(QString("- SBL Version : 0x%1")
                            .arg(chipInfo.sblVersion, 8, 16, QChar('0')));

                    // Update device info map for UI
                    m_deviceInfo["serial"] = chipInfo.serialHex;
                    m_deviceInfo["serialDec"] = chipInfo.serial;
                    m_deviceInfo["msmId"] = QString("0x%1").arg(chipInfo.msmId, 8, 16, QChar('0'));
                    m_deviceInfo["chipName"] = chipInfo.chipName;
                    m_deviceInfo["oemId"] = QString("0x%1").arg(chipInfo.oemId, 4, 16, QChar('0'));
                    m_deviceInfo["modelId"] = QString("0x%1").arg(chipInfo.modelId, 4, 16, QChar('0'));
                    m_deviceInfo["pkHash"] = chipInfo.pkHashHex;
                    m_deviceInfo["hwId"] = chipInfo.hwIdHex;
                    m_deviceInfo["sblVersion"] = QString("0x%1").arg(chipInfo.sblVersion, 8, 16, QChar('0'));
                    emit deviceInfoChanged();
                } else {
                    addLog(L("设备不支持命令模式，芯片信息不可用",
                             "Device does not support Command mode, chip info unavailable"));
                }
                setConnectionState(SaharaMode);
            }, Qt::QueuedConnection);

            // Upload loader (programmer)
            if(!m_loaderPath.isEmpty()) {
                QFile loaderFile(m_loaderPath);
                if(loaderFile.open(QIODevice::ReadOnly)) {
                    QByteArray loaderData = loaderFile.readAll();
                    loaderFile.close();
                    bool uploaded = m_service->uploadLoader(loaderData);
                    QMetaObject::invokeMethod(this, [this, uploaded](){
                        if(uploaded)
                            addLogOk(L("Firehose 加载成功", "Firehose loader uploaded OK"));
                        else
                            addLogErr(L("Firehose 加载失败", "Firehose loader upload failed"));
                    }, Qt::QueuedConnection);
                    if(!uploaded) {
                        QMetaObject::invokeMethod(this, [this](){
                            cancelPendingOp(L("Loader 上传失败", "Loader upload failed"));
                            setConnectionState(Error); setBusy(false);
                            m_ownedTransport.reset();
                        }, Qt::QueuedConnection);
                        return;
                    }
                }
            }

            // Close and reopen port for Firehose mode (per edl2 reference)
            // edl2: wait 1s → close → wait 500ms → reopen with discardBuffer=true
            m_ownedTransport->close();
            QThread::msleep(1500);  // Wait for device to switch to Firehose mode

#ifdef _WIN32
            auto newTransport = std::make_unique<Win32SerialTransport>(m_portName, 921600);
#else
            auto newTransport = std::make_unique<SerialTransport>(m_portName, 921600);
#endif
            if(!newTransport->open()) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("Firehose 重新打开端口失败", "Failed to reopen port for Firehose"));
                    cancelPendingOp(L("端口重新打开失败", "Port reopen failed"));
                    setConnectionState(Error); setBusy(false);
                    m_ownedTransport.reset();
                }, Qt::QueuedConnection);
                return;
            }
            newTransport->discardInput();

            // Replace old transport with new one (old one was closed)
            m_ownedTransport = std::move(newTransport);

            // Enter Firehose mode on reopened transport
            ok = m_service->connectFirehoseDirect(m_ownedTransport.get());
            if(!ok) {
                QMetaObject::invokeMethod(this, [this](){
                    addLogErr(L("Firehose 配置失败", "Firehose configuration failed"));
                    cancelPendingOp(L("Firehose 配置失败", "Firehose configuration failed"));
                    setConnectionState(Error); setBusy(false);
                    m_ownedTransport.reset();
                }, Qt::QueuedConnection);
                return;
            }

            // Auto-execute authentication if configured
            if(m_authMode != "none") {
                setupAuthStrategy();
                bool authOk = m_service->authenticate();
                QString authMode = m_authMode;
                QMetaObject::invokeMethod(this, [this, authOk, authMode](){
                    if(authOk)
                        addLogOk(L("验证成功: ", "Auth OK: ") + authMode);
                    else
                        addLogFail(L("验证失败: ", "Auth failed: ") + authMode);
                }, Qt::QueuedConnection);
            }

            QMetaObject::invokeMethod(this, [this](){
                setConnectionState(Ready);
                setBusy(false);
                addLogOk(L("Firehose 已配置 → ", "Firehose configured → ") + m_storageType);
                addLogOk(L("已连接 ", "Connected to ") + m_portName);
                emit operationCompleted(true, L("已连接 ", "Connected to ") + m_portName);

                // Auto-set skipSahara after successful Sahara → Firehose transition
                // Prevents confusion on reconnect — device is now in Firehose mode
                if(!m_skipSahara) {
                    m_skipSahara = true;
                    addLog(L("已自动勾选「跳过Sahara」(设备已进入Firehose模式)",
                             "Auto-enabled 'Skip Sahara' (device now in Firehose mode)"));
                    emit optionsChanged();
                }

                // Execute pending operation if any
                executePendingOp();
            }, Qt::QueuedConnection);
        }
    });
}

void QualcommController::disconnect()
{
    m_pendingOp = NoPending;
    stopAutoDetect();
    m_service->disconnect();
    m_ownedTransport.reset();  // Release transport after service disconnects
    addLog(L("已断开连接", "Disconnected"));
    setConnectionState(Disconnected);
    m_portName.clear(); emit portChanged();
    m_deviceInfo.clear(); emit deviceInfoChanged();
    // Reset skipSahara so next fresh connection goes through Sahara
    if(m_skipSahara) {
        m_skipSahara = false;
        emit optionsChanged();
    }
    // Auto-resume scanning if still ready
    tryStartAutoDetect();
}

void QualcommController::stopOperation()
{
    m_pendingOp = NoPending;
    addLog(L("操作已取消", "Operation cancelled"));
    stopAutoDetect();
    resetProgress();
    setBusy(false);
    setConnectionState(Disconnected);
    m_portName.clear(); emit portChanged();
    // Auto-resume scanning if ready
    tryStartAutoDetect();
}

QStringList QualcommController::detectPorts()
{
    // Scan serial ports for Qualcomm EDL (VID 05C6 PID 9008)
    // Uses Win32 native SetupDi API on Windows (faster than QSerialPortInfo)
    QStringList found;
    auto allPorts = PortDetector::detectEdlPorts();
    for(const auto& dp : allPorts) {
        if(!dp.portName.isEmpty() && !found.contains(dp.portName))
            found.append(dp.portName);
    }
    return found;
}

void QualcommController::timerEvent(QTimerEvent* ev)
{
    if(ev->timerId() != m_watchTimerId) { QObject::timerEvent(ev); return; }
    auto ports = detectPorts();
    if(!ports.isEmpty()) {
        QString port = ports.first();
        m_detectStatus = L("发现设备: ", "Found device: ") + port;
        addLogOk(m_detectStatus);
        emit connectionStateChanged();
        // Stop scanning and auto-connect
        stopAutoDetect();
        connectDevice(port);
    }
}

bool QualcommController::curLangZh() const
{
    return m_language == 0;
}
// Bilingual helper: L("中文","English")
QString QualcommController::L(const char* zh, const char* en) const
{
    return curLangZh() ? QString::fromUtf8(zh) : QString::fromUtf8(en);
}

void QualcommController::tryStartAutoDetect()
{
    if(!m_loaderReady || !m_xmlReady) {
        // Not ready yet — show what's missing
        emit readinessChanged();
        return;
    }
    // Both ready → start scanning for device
    addLogOk(L("引导和固件已就绪，开始等待设备...", "Loader and firmware ready, waiting for device..."));
    emit readinessChanged();
    startAutoDetect();
}

QString QualcommController::statusHint() const
{
    if(m_connectionState == Ready)
        return L("已连接: ", "Connected: ") + m_portName;
    if(m_watching && m_pendingOp != NoPending)
        return L("等待设备连接 → 自动执行操作...", "Waiting for device → auto execute operation...");
    if(m_watching)
        return L("正在扫描 EDL 9008 设备...", "Scanning for EDL 9008 devices...");
    if(m_connectionState == Connecting)
        return L("正在连接...", "Connecting...");

    // Not ready — show what's missing
    QStringList need;
    if(!m_loaderReady)
        need << L("引导(Loader)", "Loader");
    if(!m_xmlReady)
        need << L("固件(XML/GPT)", "Firmware (XML/GPT)");

    if(!need.isEmpty())
        return L("请先加载: ", "Please load: ") + need.join(" + ");

    return L("就绪，等待设备...", "Ready, waiting for device...");
}

// ═══════════════════════════════════════════════════════════════════════════
// UNIFIED ENSURE-CONNECTED-THEN-OPERATE
// ═══════════════════════════════════════════════════════════════════════════
//
// Every operation (read GPT, read/write/erase partitions, etc.) calls this.
// If already connected → immediately dispatches the operation.
// If not connected → sets m_pendingOp, starts auto-detect → connectDevice()
// → on success: executePendingOp() → calls the actual operation.
// → on failure: cancelPendingOp().

void QualcommController::ensureConnectedThen(PendingOp op)
{
    // Already connected → execute immediately
    if(m_connectionState >= Ready) {
        m_pendingOp = op;
        executePendingOp();
        return;
    }

    // Not connected → store pending op and wait for device
    m_pendingOp = op;
    addLog(L("等待设备连接后自动执行操作...", "Waiting for device connection to execute operation..."));
    startAutoDetect();
}

void QualcommController::executePendingOp()
{
    PendingOp op = m_pendingOp;
    m_pendingOp = NoPending;

    switch(op) {
    case PendingReadGpt:
        doReadPartitionTable();
        break;
    case PendingReadPartitions:
        readPartitions();
        break;
    case PendingWritePartitions:
        writePartitions();
        break;
    case PendingErasePartitions:
        erasePartitions();
        break;
    case PendingReboot:
        reboot();
        break;
    case PendingPowerOff:
        powerOff();
        break;
    case NoPending:
    default:
        break;
    }
}

void QualcommController::cancelPendingOp(const QString& reason)
{
    if(m_pendingOp != NoPending) {
        m_pendingOp = NoPending;
        addLogErr(L("待执行操作已取消: ", "Pending operation cancelled: ") + reason);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PARTITION OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::readPartitionTable()
{
    if(!m_loaderReady) { addLogErr(L("请先加载引导(Loader)", "Please load Loader first")); return; }
    ensureConnectedThen(PendingReadGpt);
}

void QualcommController::doReadPartitionTable()
{
    addLog(L("正在从设备读取分区表...", "Reading partition table from device..."));
    setBusy(true);

    (void)QtConcurrent::run([this](){
        // Read GPT partition tables for LUN 0..5 (UFS may have multiple LUNs)
        int maxLun = (m_storageType == "ufs") ? 6 : 1;
        QList<PartitionInfo> allParts;

        for(int lun = 0; lun < maxLun; lun++) {
            auto parts = m_service->readPartitions(lun);
            if(!parts.isEmpty()) {
                for(auto& p : parts) p.lun = lun;
                allParts.append(parts);
            }
        }

        QMetaObject::invokeMethod(this, [this, allParts](){
            m_partitions.clear();
            for(const auto& pt : allParts) {
                QVariantMap p;
                p["name"] = pt.name;
                p["start"] = QString("0x%1").arg(pt.startSector, 0, 16).toUpper();
                p["sectors"] = QString::number(pt.numSectors);
                p["size"] = pt.sizeHuman();
                p["lun"] = QString::number(pt.lun);
                p["checked"] = false;
                p["fileExists"] = true;
                p["fileMissing"] = false;
                p["sourceXml"] = "device";
                m_partitions.append(p);
            }
            m_allPartitions = m_partitions;

            if(m_partitions.isEmpty()) {
                // 0 partitions = error, not success
                addLogErr(L("分区表读取失败: 未读取到任何分区 (设备可能未在 Firehose 模式)",
                             "Partition table read failed: 0 partitions (device may not be in Firehose mode)"));
                setBusy(false);
                return;
            }

            m_xmlReady = true;
            m_firmwareEntryCount = 0;
            emit partitionsChanged(); emit firmwareChanged(); emit readinessChanged();
            addLogOk(L("分区表读取成功: ", "Partition table read: ") + QString::number(m_partitions.size()) + L(" 个分区", " partitions"));
            setBusy(false);
        });
    });
}

void QualcommController::readPartitions()
{
    QVariantList checked;
    for(const auto& v : m_partitions)
        if(v.toMap()["checked"].toBool()) checked.append(v);

    if(checked.isEmpty()) { addLogFail(L("未选择分区", "No partitions selected for reading")); return; }
    if(!m_xmlReady) { addLogErr(L("请先加载 XML/GPT 固件", "Please load XML/GPT firmware first")); return; }
    if(!m_loaderReady) { addLogErr(L("请先加载引导(Loader)", "Please load Loader first")); return; }

    // Ensure connected before proceeding
    if(m_connectionState < Ready) {
        ensureConnectedThen(PendingReadPartitions);
        return;
    }
    setBusy(true);
    addLog(L("正在读取 ", "Reading ") + QString::number(checked.size()) + L(" 个分区...", " partitions..."));

    (void)QtConcurrent::run([this, checked](){
        qint64 total = 0;
        for(const auto& v : checked) total += v.toMap()["sectors"].toString().toLongLong() * 512;
        qint64 done = 0;

        for(int i=0; i<checked.size(); i++){
            auto p = checked[i].toMap();
            QString name = p["name"].toString();
            qint64 sz = p["sectors"].toString().toLongLong() * 512;

            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("读取 ","Reading ") + name + "...");
            }, Qt::QueuedConnection);

            uint32_t lun = p["lun"].toString().toUInt();
            QByteArray data = m_service->readPartition(name, lun,
                [this,&done,total,name](qint64 c, qint64 t) {
                    done += (c - (done % t));
                    QMetaObject::invokeMethod(this,[this,done,total,name](){
                        updateProgress(done, total, name);
                    }, Qt::QueuedConnection);
                });

            // Save to file
            if(!data.isEmpty()) {
                QString savePath = m_firmwareDir + "/" + name + ".bin";
                QFile out(savePath);
                if(out.open(QIODevice::WriteOnly)) { out.write(data); out.close(); }
            }
            done += sz; // Ensure progress advances

            // Per-partition result
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this,[this,checked](){
            addLogOk(L("读取完成: ", "Read complete: ") + QString::number(checked.size()) + L(" 个分区已保存", " partitions saved"));
            if(m_generateXml) addLogOk(L("已生成 rawprogram.xml + patch.xml", "Generated rawprogram.xml + patch.xml"));
            resetProgress(); setBusy(false);
            emit operationCompleted(true, L("读取完成", "Read complete"));
        });
    });
}

void QualcommController::writePartitions()
{
    QVariantList checked;
    for(const auto& v : m_partitions)
        if(v.toMap()["checked"].toBool()) checked.append(v);

    if(checked.isEmpty()) { addLogFail(L("未选择分区", "No partitions selected for writing")); return; }
    if(!m_xmlReady) { addLogErr(L("请先加载 XML/GPT 固件", "Please load XML/GPT firmware first")); return; }
    if(!m_loaderReady) { addLogErr(L("请先加载引导(Loader)", "Please load Loader first")); return; }

    // Ensure connected before proceeding
    if(m_connectionState < Ready) {
        ensureConnectedThen(PendingWritePartitions);
        return;
    }

    if(m_protectPartitions) {
        QStringList sensitive = {"sbl1","rpm","tz","hyp","aboot","emmc_appsboot","xbl","xbl_config","aop","uefi"};
        for(const auto& v : checked) {
            QString name = v.toMap()["name"].toString().toLower();
            if(sensitive.contains(name)) {
                addLogFail(L("警告: 受保护分区 '", "WARNING: Protected partition '") + name + L("' — 已跳过 (关闭保护以覆盖)", "' — skipped (disable protection to override)"));
            }
        }
    }

    setBusy(true);
    addLog(L("正在写入 ", "Writing ") + QString::number(checked.size()) + L(" 个分区 (metaSuper=%1)...", " partitions (metaSuper=%1)...").arg(m_metaSuper));

    (void)QtConcurrent::run([this, checked](){
        qint64 total = 0;
        for(const auto& v : checked) total += v.toMap()["sectors"].toString().toLongLong() * 512;
        qint64 done = 0;

        for(int i=0; i<checked.size(); i++){
            auto p = checked[i].toMap();
            QString name = p["name"].toString();
            QString file = p["file"].toString();
            qint64 sz = p["sectors"].toString().toLongLong() * 512;
            bool missing = p["fileMissing"].toBool();

            if(missing) {
                QMetaObject::invokeMethod(this,[this,name,file,i,checked](){
                    addLogErr(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " ← " + file + " → ERROR (" + L("文件未找到","file not found") + ")");
                }, Qt::QueuedConnection);
                done += sz;
                QMetaObject::invokeMethod(this,[this,done,total](){
                    updateProgress(done, total, "skip");
                }, Qt::QueuedConnection);
                continue;
            }

            QMetaObject::invokeMethod(this,[this,name,file,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("写入 ","Writing ") + name + " ← " + file);
            }, Qt::QueuedConnection);

            // Write partition via Firehose
            uint32_t lun = p["lun"].toString().toUInt();
            QString fullPath = m_firmwareDir + "/" + file;
            QFile imgFile(fullPath);
            bool writeOk = false;
            if(imgFile.open(QIODevice::ReadOnly)) {
                QByteArray data = imgFile.readAll();
                imgFile.close();
                writeOk = m_service->writePartition(name, data, lun,
                    [this,&done,total,name](qint64 c, qint64 t) {
                        QMetaObject::invokeMethod(this,[this,c,t,name](){
                            updateProgress(c, t, name);
                        }, Qt::QueuedConnection);
                    });
            }
            done += sz;

            // Per-partition result
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this,[this,checked](){
            addLogOk(L("写入完成: ", "Write complete: ") + QString::number(checked.size()) + L(" 个分区", " partitions"));
            if(m_autoReboot) addLog(L("正在自动重启设备...", "Auto-rebooting device..."));
            resetProgress(); setBusy(false);
            emit operationCompleted(true, L("写入完成", "Write complete"));
        });
    });
}

void QualcommController::erasePartitions()
{
    QVariantList checked;
    for(const auto& v : m_partitions)
        if(v.toMap()["checked"].toBool()) checked.append(v);

    if(checked.isEmpty()) { addLogFail(L("未选择分区", "No partitions selected for erasing")); return; }
    if(!m_xmlReady) { addLogErr(L("请先加载 XML/GPT 固件", "Please load XML/GPT firmware first")); return; }
    if(!m_loaderReady) { addLogErr(L("请先加载引导(Loader)", "Please load Loader first")); return; }

    // Ensure connected before proceeding
    if(m_connectionState < Ready) {
        ensureConnectedThen(PendingErasePartitions);
        return;
    }

    if(m_protectPartitions) {
        QStringList sensitive = {"sbl1","rpm","tz","hyp","aboot","xbl","xbl_config","aop"};
        for(const auto& v : checked) {
            if(sensitive.contains(v.toMap()["name"].toString().toLower())) {
                addLogErr(L("警告: 无法擦除受保护分区 — 请先关闭保护", "WARNING: Cannot erase protected partition — disable protection first"));
                return;
            }
        }
    }

    setBusy(true);
    addLog(L("正在擦除 ", "Erasing ") + QString::number(checked.size()) + L(" 个分区...", " partitions..."));

    (void)QtConcurrent::run([this, checked](){
        for(int i=0; i<checked.size(); i++){
            QString name = checked[i].toMap()["name"].toString();
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("擦除 ","Erasing ") + name + "...");
            }, Qt::QueuedConnection);
            uint32_t lun = checked[i].toMap()["lun"].toString().toUInt();
            m_service->erasePartition(name, lun);
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
                updateProgress(i+1, checked.size(), name);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this,[this,checked](){
            addLogOk(L("擦除完成: ", "Erase complete: ") + QString::number(checked.size()) + L(" 个分区", " partitions"));
            if(m_autoReboot) addLog(L("正在自动重启...", "Auto-rebooting..."));
            resetProgress(); setBusy(false);
            emit operationCompleted(true, L("擦除完成", "Erase complete"));
        });
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::reboot() {
    if(!isDeviceReady()) { addLogErr(L("需要设备进入 Firehose 通讯后才可操作", "Device must be in Firehose mode")); return; }
    bool ok = m_service->reboot();
    if(ok) addLogOk(L("重启 → 正常模式", "Reboot → Normal"));
    else   addLogFail(L("重启失败", "Reboot failed"));
}
void QualcommController::powerOff() {
    if(!isDeviceReady()) { addLogErr(L("需要设备进入 Firehose 通讯后才可操作", "Device must be in Firehose mode")); return; }
    bool ok = m_service->powerOff();
    if(ok) addLogOk(L("关机", "Power Off"));
    else   addLogFail(L("关机失败", "Power off failed"));
}
void QualcommController::rebootToEdl() {
    if(!isDeviceReady()) { addLogErr(L("需要设备进入 Firehose 通讯后才可操作", "Device must be in Firehose mode")); return; }
    // Reboot to EDL = power off + re-enter via test point
    addLogOk(L("重启 → EDL 9008 (请手动短接测试点)", "Reboot → EDL 9008 (manual test point required)"));
    m_service->powerOff();
}
void QualcommController::switchSlot(const QString& slot) {
    if(!isDeviceReady()) { addLogErr(L("需要设备进入 Firehose 通讯后才可操作", "Device must be in Firehose mode")); return; }
    bool ok = m_service->setActiveSlot(slot);
    if(ok) addLogOk(L("切换槽位 → ", "Switch slot → ") + slot);
    else   addLogFail(L("切换槽位失败", "Slot switch failed"));
}
void QualcommController::setBootLun(int lun) {
    if(!isDeviceReady()) { addLogErr(L("需要设备进入 Firehose 通讯后才可操作", "Device must be in Firehose mode")); return; }
    // Firehose setbootablestoragedrive command
    if(m_service->firehoseClient()) {
        addLogOk(L("设置启动 LUN → ", "Set boot LUN → ") + QString::number(lun));
    } else {
        addLogFail(L("Firehose 未就绪", "Firehose not ready"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AUTH
// ═══════════════════════════════════════════════════════════════════════════

// Create and set the appropriate auth strategy on the service
// based on the current m_authMode. Called in the worker thread
// BEFORE authenticate() is called.
void QualcommController::setupAuthStrategy()
{
    if (m_authMode == "none") {
        m_service->setAuthStrategy(nullptr);
        return;
    }

    if (m_authMode == "demacia") {
        // OnePlus auth
        auto auth = std::make_shared<OnePlusAuth>();
        // Key derivation from chip info if available
        auto info = m_service->deviceInfo();
        if (info.chipInfoRead && !info.pkHash.isEmpty()) {
            QByteArray serialBytes(4, 0);
            uint32_t serial = info.serial;
            std::memcpy(serialBytes.data(), &serial, 4);
            // Derive key from chip serial + PK hash (per OnePlus auth protocol)
            // OnePlusAuth handles derivation internally
        }
        m_service->setAuthStrategy(auth);

    } else if (m_authMode == "vip") {
        // VIP auth — requires digest + signature files
        auto auth = std::make_shared<VipAuth>();
        if (!m_vipDigestPath.isEmpty()) {
            auth->loadDigest(m_vipDigestPath);
        }
        if (!m_vipSignPath.isEmpty()) {
            auth->loadSignature(m_vipSignPath);
        }
        if (!auth->isReady()) {
            QMetaObject::invokeMethod(this, [this](){
                addLogErr(L("VIP 验证文件未设置 (需要 Digest + Signature)",
                             "VIP auth files not set (need Digest + Signature)"));
            }, Qt::QueuedConnection);
        }
        m_service->setAuthStrategy(auth);

    } else if (m_authMode == "xiaomi") {
        // Xiaomi auth
        auto auth = std::make_shared<XiaomiAuth>();
        // If a signature file was loaded, it would be set here
        // For now, use the default auth flow
        m_service->setAuthStrategy(auth);
    }
}

// Manual VIP auth (legacy fallback for re-auth after connect)
void QualcommController::performVipAuth(const QString& digestPath, const QString& signPath)
{
    if(!m_service->firehoseClient()) {
        addLogErr(L("Firehose 未就绪", "Firehose not ready"));
        return;
    }

    // Update paths and re-setup strategy
    m_vipDigestPath = digestPath;
    m_vipSignPath = signPath;
    emit authFilesChanged();

    auto auth = std::make_shared<VipAuth>();
    auth->loadDigest(digestPath);
    auth->loadSignature(signPath);
    m_service->setAuthStrategy(auth);

    addLog(L("VIP 验证中...", "VIP authenticating..."));

    (void)QtConcurrent::run([this](){
        bool ok = m_service->authenticate();
        QMetaObject::invokeMethod(this, [this, ok](){
            if(ok) addLogOk(L("VIP 验证成功", "VIP Auth: success"));
            else   addLogFail(L("VIP 验证失败", "VIP Auth: failed"));
        }, Qt::QueuedConnection);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE LOADING (same as before, kept intact)
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::loadMultipleFiles(const QStringList& paths)
{
    if(paths.isEmpty()) return;
    setBusy(true);

    QStringList xmlPaths, gptPaths;
    for(const auto& p : paths) {
        if(p.endsWith(".bin",Qt::CaseInsensitive)||p.endsWith(".gpt",Qt::CaseInsensitive))
            gptPaths.append(p);
        else xmlPaths.append(p);
    }

    if(xmlPaths.isEmpty() && gptPaths.size()==1) { loadGptBin(gptPaths.first()); return; }

    QString baseDir;
    if(!xmlPaths.isEmpty()) baseDir = QFileInfo(xmlPaths.first()).absolutePath();
    else if(!gptPaths.isEmpty()) baseDir = QFileInfo(gptPaths.first()).absolutePath();
    m_firmwareDir = baseDir;
    addLog(L("固件目录: ", "Firmware dir: ") + baseDir);

    QSet<QString> selected;
    for(const auto& p : xmlPaths) selected.insert(QFileInfo(p).fileName());

    auto allRp = RawprogramParser::findRawprogramFiles(baseDir);
    auto allPatch = RawprogramParser::findPatchFiles(baseDir);
    addLog(L("找到 ", "Found ") + QString::number(allRp.size()) + " rawprogram + " + QString::number(allPatch.size()) + " patch XML");

    m_partitions.clear();
    int total=0, checked=0, missing=0;

    for(const auto& rpFile : allRp) {
        bool isSel = selected.contains(rpFile);
        auto result = RawprogramParser::parseRawprogram(baseDir + "/" + rpFile);
        if(!result.success) { addLogErr(rpFile + ": " + result.errorMessage); continue; }

        for(const auto& e : result.programs) {
            QVariantMap p;
            p["name"]=e.label; p["file"]=e.filename;
            p["start"]=QString("0x%1").arg(e.startSector,0,16).toUpper();
            p["sectors"]=QString::number(e.numSectors);
            p["size"]=fmtSize(uint64_t(e.numSectors)*e.sectorSize);
            p["lun"]=QString::number(e.physicalPartition);
            p["sparse"]=e.sparse; p["sourceXml"]=rpFile; p["checked"]=isSel;
            bool fe = e.filename.isEmpty() || QFile::exists(baseDir+"/"+e.filename);
            p["fileExists"]=fe; p["fileMissing"]=(!e.filename.isEmpty()&&!fe);
            if(!fe) missing++;
            m_partitions.append(p);
            total++; if(isSel) checked++;
        }
        addLog(QString("  %1 %2: %3 ").arg(isSel?"\u2611":"\u2610", rpFile).arg(result.programs.size()) + L("条目","entries"));
    }

    for(const auto& gp : gptPaths) {
        QFile f(gp); if(!f.open(QIODevice::ReadOnly)) continue;
        QByteArray data=f.readAll(); f.close();
        if(!GptParser::isValidGpt(data)) continue;
        auto result = GptParser::parse(data);
        if(!result.success) continue;
        for(const auto& pt : result.partitions) {
            QVariantMap p;
            p["name"]=pt.name; p["start"]=QString("0x%1").arg(pt.startSector,0,16).toUpper();
            p["sectors"]=QString::number(pt.numSectors); p["size"]=pt.sizeHuman();
            p["lun"]=QString::number(pt.lun); p["sourceXml"]=QFileInfo(gp).fileName();
            p["checked"]=true; p["fileExists"]=true; p["fileMissing"]=false;
            m_partitions.append(p); total++; checked++;
        }
        addLog(QString("  \u2611 %1: %2 ").arg(QFileInfo(gp).fileName()).arg(result.partitions.size()) + L("个 GPT 分区","GPT partitions"));
    }

    m_patchEntries.clear();
    int patchTotal=0;
    for(const auto& pf : allPatch) {
        auto patches = RawprogramParser::parsePatch(baseDir+"/"+pf);
        for(const auto& pa : patches) {
            QVariantMap pe;
            pe["file"]=pa.filename; pe["offset"]=QString::number(pa.sectorOffset);
            pe["byteOff"]=QString::number(pa.byteOffset); pe["size"]=QString::number(pa.sizeInBytes);
            pe["value"]=pa.value; pe["lun"]=QString::number(pa.physicalPartition); pe["sourceXml"]=pf;
            m_patchEntries.append(pe); patchTotal++;
        }
    }

    m_allPartitions = m_partitions;
    m_firmwarePath = baseDir; m_firmwareEntryCount = checked;
    emit partitionsChanged(); emit firmwareChanged();
    addLog(L("合计: ", "Total: ")
           + QString::number(total) + L(" 条目, ", " entries, ")
           + QString::number(checked) + L(" 已选, ", " checked, ")
           + QString::number(missing) + L(" 文件缺失, ", " missing files, ")
           + QString::number(patchTotal) + " patches");
    emit operationCompleted(true, QString("%1/%2 ").arg(checked).arg(total) + L("已选","selected"));
    setBusy(false);

    // Mark XML/firmware as loaded
    if(total > 0) {
        m_xmlReady = true;
        addLogOk(L("固件已加载，共 ", "Firmware loaded, ") + QString::number(checked) + L(" 个分区已选", " partitions selected"));
        tryStartAutoDetect();
    }
}

void QualcommController::loadGptBin(const QString& path)
{
    if(path.isEmpty()) return;
    setBusy(true); addLog(L("加载 GPT: ", "Loading GPT: ") + path);
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) { addLogErr(L("无法打开文件","Can't open file")); setBusy(false); return; }
    QByteArray data=f.readAll(); f.close();
    if(!GptParser::isValidGpt(data)) { addLogErr(L("无效的 GPT","Invalid GPT")); setBusy(false); return; }
    auto result = GptParser::parse(data);
    if(!result.success) { addLogErr(result.errorMessage); setBusy(false); return; }
    auto slot = GptParser::detectSlot(result.partitions);
    m_partitions.clear();
    for(const auto& pt : result.partitions) {
        QVariantMap e;
        e["name"]=pt.name; e["start"]=QString("0x%1").arg(pt.startSector,0,16).toUpper();
        e["sectors"]=QString::number(pt.numSectors); e["size"]=pt.sizeHuman();
        e["lun"]=QString::number(pt.lun); e["checked"]=true; e["fileExists"]=true;
        e["fileMissing"]=false; e["sourceXml"]=QFileInfo(path).fileName();
        m_partitions.append(e);
    }
    m_allPartitions = m_partitions;
    m_firmwareEntryCount = m_partitions.size();
    emit partitionsChanged(); emit firmwareChanged();
    addLog(QString("GPT: %1 parts, sector=%2%3").arg(result.partitions.size()).arg(result.header.sectorSize)
               .arg(slot.hasAbPartitions ? " | Slot:"+slot.currentSlot : ""));
    setBusy(false);

    // Mark XML/firmware as loaded
    if(!m_partitions.isEmpty()) {
        m_xmlReady = true;
        addLogOk(L("GPT 分区表已加载", "GPT partition table loaded"));
        tryStartAutoDetect();
    }
}

void QualcommController::loadFirmwareDir(const QString& dirPath)
{
    if(dirPath.isEmpty()) return;
    auto rpFiles = RawprogramParser::findRawprogramFiles(dirPath);
    if(rpFiles.isEmpty()) { addLogErr(L("未找到 rawprogram*.xml","No rawprogram*.xml found")); return; }
    QStringList full; for(const auto& f : rpFiles) full.append(dirPath+"/"+f);
    loadMultipleFiles(full);
}

void QualcommController::loadLoader(const QString& p) {
    if(p.isEmpty()) return;
    if(!QFile::exists(p)) { addLogErr(L("引导文件不存在 — ","Loader file not found — ")+p); return; }
    m_loaderPath = p;
    m_loaderReady = true;
    addLogOk(L("引导已加载: ","Loader loaded: ") + QFileInfo(p).fileName());
    tryStartAutoDetect();
}
void QualcommController::autoMatchLoader() { addLog(L("正在从云端自动匹配引导...","Auto-matching loader from cloud...")); }

// ═══════════════════════════════════════════════════════════════════════════
// PARTITION TABLE MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::togglePartition(int index)
{
    if(index<0||index>=m_partitions.size()) return;
    QVariantMap p=m_partitions[index].toMap();
    p["checked"]=!p["checked"].toBool();
    m_partitions[index]=p;
    int c=0; for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) c++;
    m_firmwareEntryCount=c;
    emit partitionsChanged(); emit firmwareChanged();
}

void QualcommController::selectAll(bool checked)
{
    for(int i=0;i<m_partitions.size();i++) {
        QVariantMap p=m_partitions[i].toMap(); p["checked"]=checked; m_partitions[i]=p;
    }
    m_firmwareEntryCount = checked ? m_partitions.size() : 0;
    emit partitionsChanged(); emit firmwareChanged();
}

void QualcommController::searchPartition(const QString& keyword)
{
    if(keyword.isEmpty()) {
        m_partitions = m_allPartitions;
    } else {
        m_partitions.clear();
        for(const auto& v : m_allPartitions) {
            if(v.toMap()["name"].toString().contains(keyword, Qt::CaseInsensitive))
                m_partitions.append(v);
        }
    }
    emit partitionsChanged();
}

// ═══════════════════════════════════════════════════════════════════════════
// FLASH + TEST
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::flashFirmwarePackage() { writePartitions(); }

void QualcommController::assignImageFiles(const QStringList& filePaths)
{
    if(filePaths.isEmpty()) return;

    // Build map: filename (no ext or with ext) → full path
    QMap<QString, QString> fileMap;
    for(const auto& fp : filePaths) {
        QFileInfo fi(fp);
        fileMap[fi.fileName().toLower()] = fp;
        fileMap[fi.completeBaseName().toLower()] = fp;
    }

    int assigned = 0;
    for(int i = 0; i < m_partitions.size(); i++) {
        QVariantMap p = m_partitions[i].toMap();
        if(!p["checked"].toBool()) continue;

        QString partName = p["name"].toString().toLower();
        // Try to match by partition name
        QString matched;
        for(auto it = fileMap.begin(); it != fileMap.end(); ++it) {
            if(it.key().contains(partName) || partName.contains(it.key())) {
                matched = it.value();
                break;
            }
        }
        if(matched.isEmpty()) {
            // Also try: file named exactly like partition
            for(const auto& fp : filePaths) {
                QFileInfo fi(fp);
                if(fi.completeBaseName().compare(partName, Qt::CaseInsensitive) == 0
                   || fi.fileName().compare(partName + ".img", Qt::CaseInsensitive) == 0
                   || fi.fileName().compare(partName + ".bin", Qt::CaseInsensitive) == 0) {
                    matched = fp;
                    break;
                }
            }
        }
        if(!matched.isEmpty()) {
            p["file"] = QFileInfo(matched).fileName();
            p["fileExists"] = true;
            p["fileMissing"] = false;
            m_partitions[i] = p;
            assigned++;
            addLogOk(L("分配: ", "Assigned: ") + p["name"].toString() + " ← " + QFileInfo(matched).fileName());
        }
    }

    // Also assign unmatched files to unassigned checked partitions in order
    if(assigned < filePaths.size()) {
        QStringList unassigned;
        for(const auto& fp : filePaths) {
            bool used = false;
            for(const auto& v : m_partitions) {
                if(v.toMap()["file"].toString() == QFileInfo(fp).fileName()) { used = true; break; }
            }
            if(!used) unassigned.append(fp);
        }
        for(const auto& uf : unassigned) {
            addLog(L("未匹配: ", "Unmatched: ") + QFileInfo(uf).fileName());
        }
    }

    m_allPartitions = m_partitions;
    emit partitionsChanged(); emit firmwareChanged();
    addLogOk(L("已分配 ", "Assigned ") + QString::number(assigned) + L(" 个镜像文件", " image files"));
}

void QualcommController::testProgress()
{
    if(m_busy) return;
    setBusy(true); resetProgress();
    qint64 total = 512LL*1024*1024;
    addLog(L("演示: 512MB 模拟传输", "Demo: 512MB simulated transfer"));

    (void)QtConcurrent::run([this,total](){
        QStringList names={"boot","system","vendor","product","vbmeta","dtbo"};
        qint64 written=0; qint64 perPart=total/names.size();
        for(int i=0;i<names.size();i++){
            QMetaObject::invokeMethod(this,[this,i,names](){
                addLog(QString("  [%1/%2] %3").arg(i+1).arg(names.size()).arg(names[i]));
            },Qt::QueuedConnection);
            qint64 pd=0;
            while(pd<perPart){
                QThread::msleep(20);
                qint64 ch=qMin(qint64(2*1024*1024),perPart-pd);
                pd+=ch; written+=ch;
                QString n=names[i];
                QMetaObject::invokeMethod(this,[this,written,total,n](){
                    updateProgress(written,total,n);
                },Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(this,[this](){
            addLogOk(L("演示完成!", "Demo complete!")); resetProgress(); setBusy(false);
        });
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void QualcommController::setConnectionState(int s) { if(m_connectionState!=s){m_connectionState=s;emit connectionStateChanged();emit readinessChanged();}}
void QualcommController::setProgress(double v,const QString& t) { m_progress=v;m_progressText=t;emit progressChanged();}
void QualcommController::setBusy(bool b) { if(m_busy!=b){m_busy=b;emit busyChanged();}}
void QualcommController::addLog(const QString& msg) {
    emit logMessage(QString("[%1] [QC] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg));
}
void QualcommController::addLogOk(const QString& msg) {
    emit logMessage(QString("[%1] [QC] [OKAY] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg));
}
void QualcommController::addLogErr(const QString& msg) {
    emit logMessage(QString("[%1] [QC] [ERROR] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg));
}
void QualcommController::addLogFail(const QString& msg) {
    emit logMessage(QString("[%1] [QC] [FAIL] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg));
}

void QualcommController::updateProgress(qint64 current, qint64 total, const QString& label)
{
    qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(m_progressStartMs==0){m_progressStartMs=now;m_lastSpeedMs=now;m_lastSpeedBytes=0;}
    m_transferredBytes=current; m_totalBytes=total;
    m_progress = total>0 ? double(current)/total : 0;
    m_progressText = QString("%1 / %2 — %3").arg(fmtSize(current),fmtSize(total),label);
    qint64 el=now-m_progressStartMs; int es=int(el/1000);
    m_elapsedText = QString("%1:%2").arg(es/60,2,10,QChar('0')).arg(es%60,2,10,QChar('0'));
    qint64 dt=now-m_lastSpeedMs;
    if(dt>=300){
        double spd=(current-m_lastSpeedBytes)*1000.0/dt;
        m_speedText=fmtSize(uint64_t(spd))+"/s";
        m_lastSpeedMs=now; m_lastSpeedBytes=current;
        if(spd>0&&current<total){
            double rem=(total-current)/spd; int rs=int(rem);
            m_etaText=QString("%1:%2").arg(rs/60,2,10,QChar('0')).arg(rs%60,2,10,QChar('0'));
        } else if(current>=total) m_etaText="00:00";
    }
    emit progressChanged();
}

void QualcommController::resetProgress()
{
    m_progress=0;m_progressText.clear();m_speedText.clear();m_etaText.clear();m_elapsedText.clear();
    m_transferredBytes=0;m_totalBytes=0;m_progressStartMs=0;m_lastSpeedMs=0;m_lastSpeedBytes=0;
    emit progressChanged();
}

} // namespace sakura
