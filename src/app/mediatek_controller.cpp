#include "mediatek_controller.h"
#include "mediatek/services/mediatek_service.h"
#include "mediatek/protocol/da_loader.h"
#include "transport/serial_transport.h"
#include "transport/port_detector.h"
#include "transport/i_transport.h"
#include "core/logger.h"
#include <QtConcurrent>
#include <QTimerEvent>
#include <QTime>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>

#ifdef _WIN32
#include "transport/win32_serial_transport.h"
#endif

namespace sakura {

static QString fmtSz(uint64_t b) {
    if(b>=(1ULL<<30)) return QString("%1 GB").arg(b/double(1ULL<<30),0,'f',2);
    if(b>=(1ULL<<20)) return QString("%1 MB").arg(b/double(1ULL<<20),0,'f',1);
    if(b>=(1ULL<<10)) return QString("%1 KB").arg(b/double(1ULL<<10),0,'f',0);
    return QString("%1 B").arg(b);
}

MediatekController::MediatekController(QObject* parent)
    : QObject(parent)
    , m_service(std::make_unique<MediatekService>())
{
    // Wire service signals
    QObject::connect(m_service.get(), &MediatekService::transferProgress,
                     this, [this](qint64 c, qint64 t) {
        updateProgress(c, t, m_progressText);
    });
    QObject::connect(m_service.get(), &MediatekService::logMessage,
                     this, [this](const QString& msg) { addLog(msg); });
}

MediatekController::~MediatekController() = default;

// ═══ i18n helpers ═══
void MediatekController::addLog(const QString& msg) { emit logMessage(QString("[%1] [MTK] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void MediatekController::addLogOk(const QString& msg) { emit logMessage(QString("[%1] [MTK] [OKAY] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void MediatekController::addLogErr(const QString& msg) { emit logMessage(QString("[%1] [MTK] [ERROR] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void MediatekController::addLogFail(const QString& msg) { emit logMessage(QString("[%1] [MTK] [FAIL] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
QString MediatekController::L(const char* z, const char* e) const { return zh()?QString::fromUtf8(z):QString::fromUtf8(e); }

void MediatekController::setProtocolType(int type) { if(m_protocolType!=type){m_protocolType=type; emit protocolTypeChanged();} }

// ═══ STATUS ═══
QString MediatekController::statusHint() const
{
    if(m_deviceState >= Ready) return L("已连接: ","Connected: ") + m_portName;
    if(m_watching) return L("正在扫描 MTK BROM 设备...","Scanning for MTK BROM devices...");
    if(m_deviceState == Handshaking) return L("正在握手...","Handshaking...");
    if(!m_daReady) return L("请先加载 DA 文件","Please load DA file");
    return L("等待 MTK 设备连接...","Waiting for MTK device...");
}

// ═══ AUTO-DETECT ═══
void MediatekController::startAutoDetect()
{
    if(m_watching) return;
    m_watching = true;
    addLog(L("正在扫描 MTK BROM/Preloader 设备 (VID 0E8D)...","Scanning for MTK BROM/Preloader devices (VID 0E8D)..."));
    setDeviceState(Scanning);
    if(!m_watchTimerId) m_watchTimerId = startTimer(500);
    emit readinessChanged();
}

void MediatekController::stopAutoDetect()
{
    if(!m_watching) return;
    m_watching = false;
    if(m_watchTimerId) { killTimer(m_watchTimerId); m_watchTimerId=0; }
    emit deviceStateChanged(); emit readinessChanged();
}

// ═══ MTK Port Mode Classification (based on MTK META UTILITY V48) ═══
enum class MtkMode { Unknown=0, Brom, Preloader, Da, Meta, Factory, Adb, Fastboot, Special };

static MtkMode classifyMtkPid(uint16_t pid)
{
    switch(pid) {
    // BROM mode (Boot ROM — lowest level)
    case 0x0003: return MtkMode::Brom;           // Standard BROM
    case 0x0002: return MtkMode::Brom;           // BROM Legacy
    // Preloader mode
    case 0x2000: return MtkMode::Preloader;      // Standard Preloader
    case 0x6000: return MtkMode::Preloader;      // Preloader variant
    case 0x0616: return MtkMode::Preloader;      // V2 Preloader
    // DA mode (Download Agent loaded)
    case 0x2001: return MtkMode::Da;             // DA mode
    case 0x2003: return MtkMode::Da;             // DA CDC
    case 0x2004: return MtkMode::Da;             // DA V2
    case 0x2005: return MtkMode::Da;             // DA V3
    case 0x00A5: return MtkMode::Da;             // DA (alt)
    case 0x00A2: return MtkMode::Da;             // DA (alt2)
    // META mode (engineering test)
    case 0x0001: return MtkMode::Meta;           // META
    case 0x2007: return MtkMode::Meta;           // META UART
    case 0x20FF: return MtkMode::Meta;           // META COM
    case 0x1010: return MtkMode::Meta;           // META USB
    case 0x1011: return MtkMode::Meta;           // META SP
    // Factory mode
    case 0x0023: return MtkMode::Factory;        // Composite / Factory
    case 0x2010: return MtkMode::Factory;        // Factory UART
    case 0x2011: return MtkMode::Factory;        // Factory CDC
    // ADB / Fastboot (not for flash, skip)
    case 0x200A: return MtkMode::Adb;
    case 0x200C: return MtkMode::Adb;
    case 0x200D: return MtkMode::Fastboot;
    // SP Flash tool
    case 0x3000: return MtkMode::Special;        // SP Flash
    case 0x2006: return MtkMode::Special;        // Composite
    default:     return MtkMode::Unknown;
    }
}

// Third-party vendor VIDs that use MTK chipsets
static bool isMtkVendor(uint16_t vid)
{
    switch(vid) {
    case 0x0E8D: return true;   // MediaTek
    case 0x1004: return true;   // LG MTK devices
    case 0x22D9: return true;   // OPPO MTK devices
    case 0x2717: return true;   // Xiaomi MTK devices
    case 0x2A45: return true;   // Meizu MTK devices
    case 0x0B05: return true;   // ASUS MTK variant
    default:     return false;
    }
}

// Check if the mode is suitable for flash connection (BROM, Preloader, DA)
static bool isFlashableMode(MtkMode mode)
{
    return mode == MtkMode::Brom || mode == MtkMode::Preloader || mode == MtkMode::Da;
}

static const char* mtkModeStr(MtkMode mode)
{
    switch(mode) {
    case MtkMode::Brom:       return "BROM";
    case MtkMode::Preloader:  return "Preloader";
    case MtkMode::Da:         return "DA";
    case MtkMode::Meta:       return "META";
    case MtkMode::Factory:    return "Factory";
    case MtkMode::Adb:        return "ADB";
    case MtkMode::Fastboot:   return "Fastboot";
    case MtkMode::Special:    return "Special";
    default:                  return "Unknown";
    }
}

// Backward-compat: any PID recognized as MTK downloadable
static bool isMtkPid(uint16_t pid)
{
    return classifyMtkPid(pid) != MtkMode::Unknown;
}

// Check if PID is a flash-capable mode
static bool isMtkFlashPid(uint16_t pid)
{
    return isFlashableMode(classifyMtkPid(pid));
}

void MediatekController::timerEvent(QTimerEvent* ev)
{
    if(ev->timerId() != m_watchTimerId) { QObject::timerEvent(ev); return; }

    // Full Device Manager scan (Ports + USB + USBDevice + Modem + Unknown + WPD + AndroidUSB + libusb)
    auto allPorts = PortDetector::detectAllPorts();

    for(const auto& dp : allPorts) {
        if(!dp.isMtk) continue;

        uint16_t vid = dp.vid;
        uint16_t pid = dp.pid;
        if(!isMtkVendor(vid)) continue;
        MtkMode mode = classifyMtkPid(pid);
        if(mode == MtkMode::Unknown) continue;

        QString portId = dp.portName;
        QString modeTag = QString::fromLatin1(mtkModeStr(mode));
        QString desc = dp.description.isEmpty() ? dp.friendlyName : dp.description;
        QString devClass = dp.deviceClass;

        if(isFlashableMode(mode)) {
            // Log device status for diagnostics
            QString statusStr;
            if(dp.devProblem != 0) {
                statusStr = QString(" [%1/Problem: %2]")
                    .arg(L("问题码","Problem")).arg(dp.devProblem);
            }

            if(dp.hasComPort) {
                // Device has COM port assigned — connect via serial
                addLogOk(L("发现 MTK 设备: ","Found MTK device: ") + portId
                         + QString(" [%1] [COM] [%2] (VID 0x%3 PID 0x%4)")
                           .arg(modeTag, devClass)
                           .arg(vid, 4, 16, QChar('0'))
                           .arg(pid, 4, 16, QChar('0'))
                         + (desc.isEmpty() ? "" : " — " + desc)
                         + statusStr);
                stopAutoDetect();
                connectDevice(portId);
                return;
            } else {
                // Device found in Device Manager but no COM port yet
                // VCOM driver may still be loading — log and keep scanning
                addLog(L("发现 MTK 设备 (等待 VCOM 驱动加载): ",
                         "Found MTK device (waiting for VCOM driver): ")
                       + QString("[%1] [%2] (VID 0x%3 PID 0x%4)")
                           .arg(modeTag, devClass)
                           .arg(vid, 4, 16, QChar('0'))
                           .arg(pid, 4, 16, QChar('0'))
                       + (desc.isEmpty() ? "" : " — " + desc)
                       + statusStr);
                // Don't stop scanning — keep waiting for COM port
            }
        } else {
            addLog(L("发现 MTK 设备 (非刷机模式): ","Found MTK device (non-flash mode): ")
                   + portId + " [" + modeTag + "] [" + devClass + "]"
                   + (desc.isEmpty() ? "" : " — " + desc));
        }
    }
}

void MediatekController::tryStartAutoDetect()
{
    if(!m_daReady) { emit readinessChanged(); return; }
    addLogOk(L("DA 已就绪，开始等待设备...","DA ready, waiting for device..."));
    emit readinessChanged();
    startAutoDetect();
}

QStringList MediatekController::detectPorts()
{
    QStringList found;
    // Device Manager scan — only return devices with COM port assigned
    auto allPorts = PortDetector::detectAllPorts();
    for(const auto& dp : allPorts) {
        if(!dp.isMtk) continue;
        if(!dp.hasComPort) continue;  // Only COM ports, no raw USB
        MtkMode mode = classifyMtkPid(dp.pid);
        if(!isFlashableMode(mode)) continue;
        QString entry = dp.portName + " [" + QString::fromLatin1(mtkModeStr(mode)) + "] [COM]";
        if(!found.contains(entry))
            found.append(entry);
    }
    return found;
}

// ═══ CONNECTION ═══
void MediatekController::connectDevice(const QString& port)
{
    if(m_busy) return;
    setBusy(true);
    m_portName = port; emit portChanged();
    setDeviceState(Handshaking);
    addLog(L("正在连接 ","Connecting ") + port + "...");

    // MTK always connects via COM port (VCOM driver + CreateFileA)
    // libusb is only used for BROM exploits, not for normal communication
    (void)QtConcurrent::run([this, port](){
        // Open serial transport using Win32 CreateFileA (lower CPU, more reliable)
#ifdef _WIN32
        auto transport = std::make_unique<Win32SerialTransport>(port, 115200);
#else
        auto transport = std::make_unique<SerialTransport>(port, 115200);
#endif
        if(!transport->open()) {
            QMetaObject::invokeMethod(this,[this,port](){
                addLogErr(L("串口打开失败: ","Serial port open failed: ") + port);
                setBusy(false); setDeviceState(Disconnected);
                startAutoDetect();
            },Qt::QueuedConnection);
            return;
        }

        // Transfer ownership to controller so transport outlives the lambda
        m_ownedTransport = std::move(transport);

        // Connect to device via MediatekService
        bool ok = m_service->connectDevice(m_ownedTransport.get());
        if(!ok) {
            QMetaObject::invokeMethod(this,[this](){
                addLogErr(L("BROM 连接失败","BROM connection failed"));
                setBusy(false); setDeviceState(Disconnected);
                startAutoDetect();
            },Qt::QueuedConnection);
            return;
        }

        // Get device info (includes handshake details)
        auto devInfo = m_service->deviceInfo();
        QMetaObject::invokeMethod(this,[this, devInfo](){
            addLogOk(devInfo.isBromMode
                ? L("BROM 握手成功 (Boot ROM 模式)", "BROM handshake OK (Boot ROM mode)")
                : L("BROM 握手成功 (Preloader 模式)", "BROM handshake OK (Preloader mode)"));
            setDeviceState(devInfo.isBromMode ? BromMode : PreloaderMode);

            m_deviceInfo["hwCode"] = QString("0x%1").arg(devInfo.hwCode, 4, 16, QChar('0'));
            m_deviceInfo["chip"] = m_service->chipName();
            m_deviceInfo["meId"] = devInfo.meId.toHex().toUpper();
            m_deviceInfo["socId"] = devInfo.socId.toHex().toUpper();
            m_deviceInfo["mode"] = devInfo.isBromMode ? "BROM" : "Preloader";
            QString secBoot = QString("SBC: %1 | SLA: %2 | DAA: %3")
                .arg(devInfo.targetCfg.sbc ? "ON" : "OFF")
                .arg(devInfo.targetCfg.slaEnabled ? "ON" : "OFF")
                .arg(devInfo.targetCfg.daaEnabled ? "ON" : "OFF");
            m_deviceInfo["secBoot"] = secBoot;
            emit deviceInfoChanged();
            addLogOk(L("芯片: ","Chip: ") + m_deviceInfo["chip"].toString()
                     + " [" + m_deviceInfo["mode"].toString() + "]");
            addLog(L("安全: ","Security: ") + secBoot);
        },Qt::QueuedConnection);

        // Load DA file if available
        if(!m_daPath.isEmpty()) {
            m_service->loadDaFile(m_daPath);
        }

        // Download DA to device
        bool daOk = m_service->downloadDa();
        if(!daOk) {
            QMetaObject::invokeMethod(this,[this](){
                addLogErr(L("DA 下载失败","DA download failed"));
                setBusy(false); setDeviceState(Error);
            },Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this,[this](){
            addLogOk(L("DA 已加载","DA loaded OK"));
            setDeviceState(Da1Loaded);
        },Qt::QueuedConnection);

        // Read partitions from device
        auto parts = m_service->readPartitions();
        QMetaObject::invokeMethod(this,[this, parts](){
            if(!parts.isEmpty()) {
                m_partitions.clear();
                for(const auto& pi : parts) {
                    QVariantMap p;
                    p["name"] = pi.name;
                    p["start"] = QString("0x%1").arg(pi.startSector, 0, 16);
                    p["size"] = fmtSz(pi.numSectors * 512);
                    p["sectors"] = QString::number(pi.numSectors);
                    p["checked"] = false;
                    p["sourceXml"] = "device";
                    m_partitions.append(p);
                }
                m_allPartitions = m_partitions;
                addLogOk(L("分区表已读取: ","Partition table read: ") + QString::number(parts.size()) + L(" 个分区"," partitions"));
                emit partitionsChanged();
            }
            setDeviceState(Ready);
            setBusy(false);
            addLogOk(L("设备已就绪","Device ready"));
            emit operationCompleted(true, L("已连接","Connected"));
        },Qt::QueuedConnection);
    });
}

void MediatekController::disconnect()
{
    stopAutoDetect();
    m_service->disconnect();
    m_ownedTransport.reset();  // Release transport after service disconnects
    addLog(L("已断开连接","Disconnected"));
    setDeviceState(Disconnected);
    m_portName.clear(); emit portChanged();
    m_deviceInfo.clear(); emit deviceInfoChanged();
    tryStartAutoDetect();
}

void MediatekController::stopOperation()
{
    addLog(L("操作已取消","Operation cancelled"));
    stopAutoDetect(); resetProgress(); setBusy(false);
    setDeviceState(Disconnected);
    tryStartAutoDetect();
}

// ═══ FILE LOADING ═══
void MediatekController::loadDaFile(const QString& path)
{
    if(path.isEmpty()) return;
    m_daPath = path; m_daReady = true;
    // Pre-load DA into service
    m_service->loadDaFile(path);
    addLogOk(L("DA 已加载: ","DA loaded: ") + QFileInfo(path).fileName());
    tryStartAutoDetect();
}

void MediatekController::loadScatterFile(const QString& path)
{
    if(path.isEmpty()) return;
    m_scatterPath = path; m_scatterReady = true;
    addLogOk(L("Scatter 已加载: ","Scatter loaded: ") + QFileInfo(path).fileName());

    // Parse scatter file to populate partitions
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) {
        addLogErr(L("无法打开 Scatter 文件","Cannot open scatter file"));
        return;
    }
    QByteArray content = f.readAll();
    f.close();

    // Scatter file format: partition_name, start_addr, size lines
    m_partitions.clear();
    QString text = QString::fromUtf8(content);
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    QString currentPart;
    uint64_t currentStart = 0, currentSize = 0;
    for(const auto& line : lines) {
        QString trimmed = line.trimmed();
        if(trimmed.startsWith("partition_name:")) {
            if(!currentPart.isEmpty()) {
                QVariantMap p;
                p["name"] = currentPart;
                p["start"] = QString("0x%1").arg(currentStart, 0, 16);
                p["size"] = fmtSz(currentSize);
                p["checked"] = false;
                p["sourceXml"] = "scatter";
                m_partitions.append(p);
            }
            currentPart = trimmed.mid(15).trimmed();
            currentStart = 0; currentSize = 0;
        } else if(trimmed.startsWith("linear_start_addr:")) {
            currentStart = trimmed.mid(18).trimmed().toULongLong(nullptr, 0);
        } else if(trimmed.startsWith("partition_size:")) {
            currentSize = trimmed.mid(15).trimmed().toULongLong(nullptr, 0);
        }
    }
    if(!currentPart.isEmpty()) {
        QVariantMap p;
        p["name"] = currentPart;
        p["start"] = QString("0x%1").arg(currentStart, 0, 16);
        p["size"] = fmtSz(currentSize);
        p["checked"] = false;
        p["sourceXml"] = "scatter";
        m_partitions.append(p);
    }

    m_allPartitions = m_partitions;
    addLogOk(L("Scatter 解析: ","Scatter parsed: ") + QString::number(m_partitions.size()) + L(" 个分区"," partitions"));
    emit partitionsChanged(); emit readinessChanged();
}

void MediatekController::loadFirmwareDir(const QString& dirPath)
{
    if(dirPath.isEmpty()) return;
    QDir dir(dirPath);
    auto scatFiles = dir.entryList({"*scatter*","*Scatter*"}, QDir::Files);
    auto daFiles = dir.entryList({"MTK_AllInOne_DA*","DA_*"}, QDir::Files);
    if(!scatFiles.isEmpty()) loadScatterFile(dirPath+"/"+scatFiles.first());
    if(!daFiles.isEmpty()) loadDaFile(dirPath+"/"+daFiles.first());
    if(scatFiles.isEmpty() && daFiles.isEmpty())
        addLogErr(L("未找到 Scatter/DA 文件","No Scatter/DA files found"));
}

// ═══ OPERATIONS ═══
void MediatekController::readPartitionTable()
{
    if(!m_daReady) { addLogErr(L("请先加载 DA 文件","Please load DA file first")); return; }
    if(!isDeviceReady()) {
        addLog(L("正在等待设备连接以读取分区表...","Waiting for device to read partition table..."));
        // Auto-detect will handle this when device connects
        startAutoDetect();
        return;
    }
    setBusy(true);
    addLog(L("正在从设备读取分区表...","Reading partition table from device..."));

    (void)QtConcurrent::run([this](){
        auto parts = m_service->readPartitions();
        QMetaObject::invokeMethod(this,[this, parts](){
            if(!parts.isEmpty()) {
                m_partitions.clear();
                for(const auto& pi : parts) {
                    QVariantMap p;
                    p["name"] = pi.name;
                    p["start"] = QString("0x%1").arg(pi.startSector, 0, 16);
                    p["size"] = fmtSz(pi.numSectors * 512);
                    p["sectors"] = QString::number(pi.numSectors);
                    p["checked"] = false;
                    p["sourceXml"] = "device";
                    m_partitions.append(p);
                }
                m_allPartitions = m_partitions;
                m_checkedCount = 0;
                addLogOk(L("分区表已读取: ","Partition table read: ") + QString::number(parts.size()) + L(" 个分区"," partitions"));
                emit partitionsChanged();
            } else {
                addLogErr(L("分区表读取失败","Failed to read partition table"));
            }
            setBusy(false);
        },Qt::QueuedConnection);
    });
}

void MediatekController::erasePartitions()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    if(!hasCheckedPartitions()) { addLogFail(L("未选择分区","No partitions selected")); return; }
    setBusy(true);
    QVariantList checked;
    for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) checked.append(v);
    addLog(L("正在擦除 ","Erasing ") + QString::number(checked.size()) + L(" 个分区...","partitions..."));

    (void)QtConcurrent::run([this,checked](){
        int ok=0, fail=0;
        for(int i=0;i<checked.size();i++){
            QString name=checked[i].toMap()["name"].toString();
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("擦除 ","Erasing ") + name + "...");
                updateProgress(i, checked.size(), name);
            },Qt::QueuedConnection);

            bool success = m_service->erasePartition(name);

            QMetaObject::invokeMethod(this,[this,name,i,checked,success](){
                if(success)
                    addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → erased");
                else
                    addLogFail(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → erase FAIL");
                updateProgress(i+1, checked.size(), name);
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,ok,fail](){
            if(fail==0)
                addLogOk(L("擦除完成: ","Erase complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("擦除: ","Erase: ") + QString::number(ok) + " OK, " + QString::number(fail) + " FAIL");
            resetProgress(); setBusy(false);
        });
    });
}

void MediatekController::readFlash()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    if(!hasCheckedPartitions()) { addLogFail(L("未选择分区","No partitions selected")); return; }
    setBusy(true);
    QVariantList checked;
    for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) checked.append(v);
    addLog(L("正在读取 ","Reading ") + QString::number(checked.size()) + L(" 个分区...","partitions..."));

    (void)QtConcurrent::run([this,checked](){
        int ok=0, fail=0;
        for(int i=0;i<checked.size();i++){
            QString name=checked[i].toMap()["name"].toString();
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("读取 ","Reading ") + name + "...");
                updateProgress(i, checked.size(), name);
            },Qt::QueuedConnection);

            QByteArray data = m_service->readPartition(name);
            bool success = !data.isEmpty();

            QMetaObject::invokeMethod(this,[this,name,i,checked,success](){
                if(success)
                    addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
                else
                    addLogFail(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → FAIL");
                updateProgress(i+1, checked.size(), name);
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,checked,ok,fail](){
            if(fail==0)
                addLogOk(L("读取完成: ","Read complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("读取完成: ","Read complete: ") + QString::number(ok) + " OK, " + QString::number(fail) + L(" 失败"," failed"));
            resetProgress(); setBusy(false);
        });
    });
}

void MediatekController::writeFlash()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    if(!hasCheckedPartitions()) { addLogFail(L("未选择分区","No partitions selected")); return; }
    setBusy(true);
    QVariantList checked;
    for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) checked.append(v);
    addLog(L("正在写入 ","Writing ") + QString::number(checked.size()) + L(" 个分区..."," partitions..."));

    (void)QtConcurrent::run([this,checked](){
        int ok=0, fail=0;
        for(int i=0;i<checked.size();i++){
            QString name=checked[i].toMap()["name"].toString();
            QString filePath=checked[i].toMap()["filePath"].toString();
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("写入 ","Writing ") + name + "...");
                updateProgress(i, checked.size(), name);
            },Qt::QueuedConnection);

            bool success = false;
            if(!filePath.isEmpty()) {
                QFile f(filePath);
                if(f.open(QIODevice::ReadOnly)) {
                    success = m_service->writePartition(name, f.readAll());
                    f.close();
                }
            }

            QMetaObject::invokeMethod(this,[this,name,i,checked,success](){
                if(success)
                    addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
                else
                    addLogFail(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → FAIL");
                updateProgress(i+1, checked.size(), name);
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,checked,ok,fail](){
            if(fail==0)
                addLogOk(L("写入完成: ","Write complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("写入完成: ","Write complete: ") + QString::number(ok) + " OK, " + QString::number(fail) + L(" 失败"," failed"));
            resetProgress(); setBusy(false);
        });
    });
}

void MediatekController::formatAll()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    setBusy(true);
    addLog(L("正在格式化全部分区...","Formatting all partitions..."));
    (void)QtConcurrent::run([this](){
        bool ok = m_service->formatAll();
        QMetaObject::invokeMethod(this,[this,ok](){
            if(ok) addLogOk(L("格式化完成","Format complete"));
            else   addLogFail(L("格式化失败","Format failed"));
            setBusy(false);
        });
    });
}

void MediatekController::readImei()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在读取 IMEI...","Reading IMEI..."));
    // Read nvram partition which contains IMEI data
    QByteArray nvdata = m_service->readPartition("nvram", 0, 512);
    if(!nvdata.isEmpty()) {
        // IMEI is typically at offset 0x4 in NVRAM, BCD encoded
        addLogOk("IMEI: " + nvdata.mid(4, 8).toHex());
    } else {
        addLogFail(L("IMEI 读取失败","IMEI read failed"));
    }
}

void MediatekController::writeImei(const QString& imei)
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在写入 IMEI: ","Writing IMEI: ") + imei);
    // Build IMEI BCD data and write to nvram partition
    QByteArray imeiData;
    for(int i = 0; i < imei.size(); i += 2) {
        int hi = imei.mid(i, 1).toInt(nullptr, 16);
        int lo = (i+1 < imei.size()) ? imei.mid(i+1, 1).toInt(nullptr, 16) : 0;
        imeiData.append(static_cast<char>((hi << 4) | lo));
    }
    bool ok = m_service->writePartition("nvram", imeiData);
    if(ok) addLogOk(L("IMEI 写入成功","IMEI write OK"));
    else   addLogFail(L("IMEI 写入失败","IMEI write failed"));
}

void MediatekController::readNvram()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在读取 NVRAM...","Reading NVRAM..."));
    setBusy(true);
    (void)QtConcurrent::run([this](){
        QByteArray data = m_service->readPartition("nvram");
        QMetaObject::invokeMethod(this,[this,data](){
            if(!data.isEmpty())
                addLogOk(L("NVRAM 读取成功: ","NVRAM read OK: ") + fmtSz(data.size()));
            else
                addLogFail(L("NVRAM 读取失败","NVRAM read failed"));
            setBusy(false);
        });
    });
}

void MediatekController::unlockBootloader()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在解锁 Bootloader...","Unlocking bootloader..."));
    // Write unlock flag to proinfo/seccfg partition
    QByteArray unlockData(4, '\0');
    unlockData[0] = 0x01; // unlock flag
    bool ok = m_service->writePartition("seccfg", unlockData);
    if(ok) addLogOk(L("Bootloader 已解锁","Bootloader unlocked"));
    else   addLogFail(L("Bootloader 解锁失败","Bootloader unlock failed"));
}

void MediatekController::reboot()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    bool ok = m_service->reboot();
    if(ok) addLogOk(L("重启设备","Rebooting device"));
    else   addLogFail(L("重启失败","Reboot failed"));
}

// ═══ PARTITION MANAGEMENT ═══
void MediatekController::togglePartition(int index)
{
    if(index<0||index>=m_partitions.size()) return;
    QVariantMap p=m_partitions[index].toMap();
    p["checked"]=!p["checked"].toBool(); m_partitions[index]=p;
    m_checkedCount=0; for(const auto& v:m_partitions) if(v.toMap()["checked"].toBool()) m_checkedCount++;
    emit partitionsChanged();
}

void MediatekController::selectAll(bool checked)
{
    for(int i=0;i<m_partitions.size();i++){QVariantMap p=m_partitions[i].toMap();p["checked"]=checked;m_partitions[i]=p;}
    m_checkedCount=checked?m_partitions.size():0;
    emit partitionsChanged();
}

// ═══ HELPERS ═══
void MediatekController::setDeviceState(int s) { if(m_deviceState!=s){m_deviceState=s;emit deviceStateChanged();emit readinessChanged();}}
void MediatekController::setBusy(bool b) { if(m_busy!=b){m_busy=b;emit busyChanged();}}

void MediatekController::updateProgress(qint64 cur, qint64 tot, const QString& label)
{
    m_progress = tot>0?double(cur)/tot:0;
    m_progressText = QString("%1/%2 — %3").arg(cur).arg(tot).arg(label);
    qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(!m_progressStartMs) m_progressStartMs=now;
    int es=int((now-m_progressStartMs)/1000);
    m_elapsedText=QString("%1:%2").arg(es/60,2,10,QChar('0')).arg(es%60,2,10,QChar('0'));
    emit progressChanged();
}

void MediatekController::resetProgress()
{
    m_progress=0;m_progressText.clear();m_speedText.clear();m_etaText.clear();m_elapsedText.clear();
    m_progressStartMs=0;m_lastSpeedMs=0;m_lastSpeedBytes=0;
    emit progressChanged();
}

} // namespace sakura
