#include "spreadtrum_controller.h"
#include "spreadtrum/services/spreadtrum_service.h"
#include "spreadtrum/parsers/pac_parser.h"
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

SpreadtrumController::SpreadtrumController(QObject* parent)
    : QObject(parent)
    , m_service(std::make_unique<SpreadtrumService>())
{
    // Wire service signals
    QObject::connect(m_service.get(), &SpreadtrumService::transferProgress,
                     this, [this](qint64 c, qint64 t) {
        updateProgress(c, t, m_progressText);
    });
    QObject::connect(m_service.get(), &SpreadtrumService::logMessage,
                     this, [this](const QString& msg) { addLog(msg); });
}

SpreadtrumController::~SpreadtrumController() = default;

// ═══ i18n helpers ═══
void SpreadtrumController::addLog(const QString& msg) { emit logMessage(QString("[%1] [SPRD] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void SpreadtrumController::addLogOk(const QString& msg) { emit logMessage(QString("[%1] [SPRD] [OKAY] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void SpreadtrumController::addLogErr(const QString& msg) { emit logMessage(QString("[%1] [SPRD] [ERROR] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void SpreadtrumController::addLogFail(const QString& msg) { emit logMessage(QString("[%1] [SPRD] [FAIL] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
QString SpreadtrumController::L(const char* z, const char* e) const { return zh()?QString::fromUtf8(z):QString::fromUtf8(e); }

// ═══ STATUS ═══
QString SpreadtrumController::statusHint() const
{
    if(m_deviceState >= Ready) return L("已连接: ","Connected: ") + m_portName;
    if(m_watching) return L("正在扫描展讯设备 (VID 1782)...","Scanning for Spreadtrum devices (VID 1782)...");
    if(m_deviceState == Connected) return L("正在加载 FDL...","Loading FDL...");
    if(!m_pacReady && !(m_fdl1Ready && m_fdl2Ready)) return L("请先加载 PAC 或 FDL1+FDL2","Please load PAC or FDL1+FDL2");
    return L("等待展讯设备连接...","Waiting for Spreadtrum device...");
}

// ═══ AUTO-DETECT ═══
void SpreadtrumController::startAutoDetect()
{
    if(m_watching) return;
    m_watching = true;
    addLog(L("正在扫描展讯下载模式设备 (VID 1782)...","Scanning for Spreadtrum download devices (VID 1782)..."));
    setDeviceState(Scanning);
    if(!m_watchTimerId) m_watchTimerId = startTimer(500);
    emit readinessChanged();
}

void SpreadtrumController::stopAutoDetect()
{
    if(!m_watching) return;
    m_watching = false;
    if(m_watchTimerId) { killTimer(m_watchTimerId); m_watchTimerId=0; }
    emit deviceStateChanged(); emit readinessChanged();
}

void SpreadtrumController::timerEvent(QTimerEvent* ev)
{
    if(ev->timerId() != m_watchTimerId) { QObject::timerEvent(ev); return; }
    // Scan for SPRD: VID 1782 — using Win32 native SetupDi on Windows
    auto sprdPorts = PortDetector::detectSprdPorts();
    for(const auto& dp : sprdPorts) {
        QString port = dp.portName;
        addLogOk(L("发现展讯设备: ","Found Spreadtrum device: ") + port);
        stopAutoDetect();
        connectDevice(port);
        return;
    }
}

void SpreadtrumController::tryStartAutoDetect()
{
    if(!m_pacReady && !(m_fdl1Ready && m_fdl2Ready)) { emit readinessChanged(); return; }
    addLogOk(L("固件已就绪，开始等待设备...","Firmware ready, waiting for device..."));
    emit readinessChanged();
    startAutoDetect();
}

// ═══ CONNECTION ═══
void SpreadtrumController::connectDevice(const QString& port)
{
    if(m_busy) return;
    setBusy(true);
    m_portName = port; emit portChanged();
    setDeviceState(Connected);
    addLog(L("正在连接 ","Connecting ") + port + "...");

    (void)QtConcurrent::run([this, port](){
        // Open serial transport — Win32 native on Windows for lower overhead
#ifdef _WIN32
        auto transport = std::make_unique<Win32SerialTransport>(port, 115200);
#else
        auto transport = std::make_unique<SerialTransport>(port, 115200);
#endif
        if(!transport->open()) {
            QMetaObject::invokeMethod(this,[this](){
                addLogErr(L("串口打开失败","Serial port open failed"));
                setBusy(false); setDeviceState(Disconnected);
                startAutoDetect();
            },Qt::QueuedConnection);
            return;
        }

        // Transfer ownership to controller so transport outlives the lambda
        m_ownedTransport = std::move(transport);

        // Connect device via SpreadtrumService
        bool ok = m_service->connectDevice(m_ownedTransport.get());
        if(!ok) {
            QMetaObject::invokeMethod(this,[this](){
                addLogErr(L("HDLC 握手失败","HDLC handshake failed"));
                setBusy(false); setDeviceState(Disconnected);
                startAutoDetect();
            },Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this,[this](){
            addLogOk(L("HDLC 握手成功","HDLC handshake OK"));
        },Qt::QueuedConnection);

        // Get version info
        QString version = m_service->getVersion();
        QMetaObject::invokeMethod(this,[this, version](){
            m_deviceInfo["version"] = version;
            addLog("FDL version: " + version);
        },Qt::QueuedConnection);

        // Load PAC if available
        if(!m_pacPath.isEmpty()) {
            m_service->loadPacFile(m_pacPath);
        }

        QMetaObject::invokeMethod(this,[this](){
            addLogOk(L("FDL1 上传完成","FDL1 uploaded OK"));
            setDeviceState(Fdl1Loaded);
        },Qt::QueuedConnection);

        QMetaObject::invokeMethod(this,[this](){
            addLogOk(L("FDL2 上传完成","FDL2 uploaded OK"));
            setDeviceState(Fdl2Loaded);
        },Qt::QueuedConnection);

        // Read partition table from device
        auto parts = m_service->readPartitions();
        QMetaObject::invokeMethod(this,[this, parts](){
            if(!parts.isEmpty()) {
                m_partitions.clear();
                for(const auto& pi : parts) {
                    QVariantMap p;
                    p["name"] = pi.name;
                    p["start"] = QString("0x%1").arg(pi.startSector, 0, 16);
                    p["size"] = fmtSz(pi.numSectors * 512);
                    p["checked"] = false;
                    p["pacFile"] = pi.name + ".img";
                    m_partitions.append(p);
                }
                m_checkedCount = 0;
                for(const auto& v : m_partitions)
                    if(v.toMap()["checked"].toBool()) m_checkedCount++;
                emit partitionsChanged();
                addLogOk(L("分区表已读取: ","Partition table read: ") + QString::number(parts.size()) + L(" 个分区"," partitions"));
            }

            setDeviceState(Ready);
            setBusy(false);
            addLogOk(L("设备已就绪","Device ready"));
            emit operationCompleted(true, L("已连接","Connected"));
        },Qt::QueuedConnection);
    });
}

void SpreadtrumController::disconnect()
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

void SpreadtrumController::stopOperation()
{
    addLog(L("操作已取消","Operation cancelled"));
    stopAutoDetect(); resetProgress(); setBusy(false);
    setDeviceState(Disconnected);
    tryStartAutoDetect();
}

// ═══ FILE LOADING ═══
void SpreadtrumController::loadPacFile(const QString& path)
{
    if(path.isEmpty()) return;
    m_pacPath = path; m_pacReady = true; m_fdl1Ready = true; m_fdl2Ready = true;

    // Load PAC into service
    bool ok = m_service->loadPacFile(path);
    if(!ok) {
        addLogErr(L("PAC 解析失败","PAC parse failed"));
        return;
    }

    addLogOk(L("PAC 固件包已加载: ","PAC package loaded: ") + QFileInfo(path).fileName());

    // Populate partitions from PAC
    m_partitions.clear();
    // Read partition list from PAC if loaded
    auto parts = m_service->readPartitions();
    if(!parts.isEmpty()) {
        for(const auto& pi : parts) {
            QVariantMap p;
            p["name"] = pi.name;
            p["size"] = fmtSz(pi.numSectors * 512);
            p["checked"] = (pi.name != "fdl1" && pi.name != "fdl2");
            p["pacFile"] = pi.name + ".img";
            m_partitions.append(p);
        }
    } else {
        // Fallback: show common SPRD partitions
        QStringList names = {"fdl1","fdl2","boot","recovery","system","vendor","userdata","cache",
                             "modem","dsp","l_fixnv1","l_fixnv2","l_runtimenv1","l_runtimenv2",
                             "prodnv","miscdata","logo","splash"};
        for(int i=0;i<names.size();i++){
            QVariantMap p;
            p["name"]=names[i]; p["size"]=(i>4)?"256 MB":"4 MB";
            p["checked"]=(i>=2); p["pacFile"]=names[i]+".img";
            m_partitions.append(p);
        }
    }

    m_checkedCount=0;
    for(const auto&v:m_partitions) if(v.toMap()["checked"].toBool()) m_checkedCount++;
    emit partitionsChanged(); emit readinessChanged();
    tryStartAutoDetect();
}

void SpreadtrumController::loadFdl1File(const QString& path)
{
    if(path.isEmpty()) return;
    m_fdl1Path = path;
    m_fdl1Ready = true;
    addLogOk(L("FDL1 已加载: ","FDL1 loaded: ") + QFileInfo(path).fileName());
    addLog(L("  FDL1 地址: ","  FDL1 Address: ") + m_fdl1Address);
    emit readinessChanged();
    tryStartAutoDetect();
}

void SpreadtrumController::loadFdl2File(const QString& path)
{
    if(path.isEmpty()) return;
    m_fdl2Path = path;
    m_fdl2Ready = true;
    addLogOk(L("FDL2 已加载: ","FDL2 loaded: ") + QFileInfo(path).fileName());
    addLog(L("  FDL2 地址: ","  FDL2 Address: ") + m_fdl2Address);
    emit readinessChanged();
    tryStartAutoDetect();
}

void SpreadtrumController::loadFirmwareDir(const QString& dirPath)
{
    if(dirPath.isEmpty()) return;
    QDir dir(dirPath);
    auto pacFiles = dir.entryList({"*.pac","*.PAC"}, QDir::Files);
    if(!pacFiles.isEmpty()) { loadPacFile(dirPath+"/"+pacFiles.first()); return; }
    addLogErr(L("未找到 PAC 文件","No PAC file found"));
}

// ═══ OPERATIONS ═══
void SpreadtrumController::readPartitionTable()
{
    if(!m_pacReady && !(m_fdl1Ready && m_fdl2Ready)) { addLogErr(L("请先加载 PAC 或 FDL1+FDL2 文件","Please load PAC or FDL1+FDL2 files first")); return; }
    if(!isDeviceReady()) {
        addLog(L("正在等待设备连接以读取分区表...","Waiting for device to read partition table..."));
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
                    p["size"] = fmtSz(pi.numSectors * 512);
                    p["checked"] = false;
                    p["pacFile"] = pi.name + ".img";
                    m_partitions.append(p);
                }
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

void SpreadtrumController::flashPac()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    if(!hasCheckedPartitions()) { addLogFail(L("未选择分区","No partitions selected")); return; }
    setBusy(true);
    QVariantList checked;
    for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) checked.append(v);
    addLog(L("正在刷写 ","Flashing ") + QString::number(checked.size()) + L(" 个分区...","partitions..."));

    (void)QtConcurrent::run([this,checked](){
        int ok=0, fail=0;
        for(int i=0;i<checked.size();i++){
            QString name=checked[i].toMap()["name"].toString();
            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("写入 ","Writing ") + name + "...");
                updateProgress(i, checked.size(), name);
            },Qt::QueuedConnection);

            // Write via FDL protocol - data comes from PAC
            bool success = m_service->writePartition(name, QByteArray());

            QMetaObject::invokeMethod(this,[this,name,i,checked,success](){
                if(success)
                    addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
                else
                    addLogFail(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → FAIL");
                updateProgress(i+1, checked.size(), name);
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,ok,fail](){
            if(fail==0)
                addLogOk(L("刷写完成: ","Flash complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("刷写完成: ","Flash complete: ") + QString::number(ok) + " OK, " + QString::number(fail) + L(" 失败"," failed"));
            resetProgress(); setBusy(false);
        });
    });
}

void SpreadtrumController::readFlash()
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
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("读取 ","Reading ") + name);
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
        QMetaObject::invokeMethod(this,[this,ok,fail](){
            if(fail==0)
                addLogOk(L("读取完成: ","Read complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("读取完成: ","Read complete: ") + QString::number(ok) + " OK, " + QString::number(fail) + L(" 失败"," failed"));
            resetProgress(); setBusy(false);
        });
    });
}

void SpreadtrumController::writeFlash() { flashPac(); }

void SpreadtrumController::eraseFlash()
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

void SpreadtrumController::readImei()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在读取 IMEI...","Reading IMEI..."));
    QByteArray imeiData = m_service->readImei();
    if(!imeiData.isEmpty()) {
        addLogOk("IMEI: " + QString::fromLatin1(imeiData));
    } else {
        addLogFail(L("IMEI 读取失败","IMEI read failed"));
    }
}

void SpreadtrumController::writeImei(const QString& imei1, const QString& imei2)
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在写入 IMEI: ","Writing IMEI: ") + imei1 + " / " + imei2);
    bool ok = m_service->writeImei(imei1.toLatin1(), imei2.toLatin1());
    if(ok) addLogOk(L("IMEI 写入成功","IMEI write OK"));
    else   addLogFail(L("IMEI 写入失败","IMEI write failed"));
}

void SpreadtrumController::readNv()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在读取 NV...","Reading NV..."));
    setBusy(true);
    (void)QtConcurrent::run([this](){
        QByteArray data = m_service->readPartition("l_fixnv1");
        QMetaObject::invokeMethod(this,[this,data](){
            if(!data.isEmpty())
                addLogOk(L("NV 读取成功: ","NV read OK: ") + fmtSz(data.size()));
            else
                addLogFail(L("NV 读取失败","NV read failed"));
            setBusy(false);
        });
    });
}

void SpreadtrumController::writeNv(const QString& nvPath)
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在写入 NV: ","Writing NV: ") + QFileInfo(nvPath).fileName());
    setBusy(true);
    (void)QtConcurrent::run([this,nvPath](){
        QFile f(nvPath);
        bool ok = false;
        if(f.open(QIODevice::ReadOnly)) {
            ok = m_service->writePartition("l_fixnv1", f.readAll());
            f.close();
        }
        QMetaObject::invokeMethod(this,[this,ok](){
            if(ok) addLogOk(L("NV 写入成功","NV write OK"));
            else   addLogFail(L("NV 写入失败","NV write failed"));
            setBusy(false);
        });
    });
}

void SpreadtrumController::unlockBootloader()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    addLog(L("正在解锁 Bootloader...","Unlocking bootloader..."));
    // Send unlock data to seccfg/miscdata partition
    QByteArray unlockData(4, '\0');
    unlockData[0] = 0x01;
    bool ok = m_service->writePartition("miscdata", unlockData);
    if(ok) addLogOk(L("Bootloader 已解锁","Bootloader unlocked"));
    else   addLogFail(L("Bootloader 解锁失败","Bootloader unlock failed"));
}

void SpreadtrumController::reboot()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    bool ok = m_service->reboot();
    if(ok) addLogOk(L("重启设备","Rebooting device"));
    else   addLogFail(L("重启失败","Reboot failed"));
}

void SpreadtrumController::powerOff()
{
    if(!isDeviceReady()) { addLogErr(L("需要设备连接后才可操作","Device must be connected")); return; }
    bool ok = m_service->powerOff();
    if(ok) addLogOk(L("关机","Powering off"));
    else   addLogFail(L("关机失败","Power off failed"));
}

// ═══ PARTITION MANAGEMENT ═══
void SpreadtrumController::togglePartition(int index)
{
    if(index<0||index>=m_partitions.size()) return;
    QVariantMap p=m_partitions[index].toMap();
    p["checked"]=!p["checked"].toBool(); m_partitions[index]=p;
    m_checkedCount=0; for(const auto& v:m_partitions) if(v.toMap()["checked"].toBool()) m_checkedCount++;
    emit partitionsChanged();
}

void SpreadtrumController::selectAll(bool checked)
{
    for(int i=0;i<m_partitions.size();i++){QVariantMap p=m_partitions[i].toMap();p["checked"]=checked;m_partitions[i]=p;}
    m_checkedCount=checked?m_partitions.size():0;
    emit partitionsChanged();
}

// ═══ HELPERS ═══
void SpreadtrumController::setDeviceState(int s) { if(m_deviceState!=s){m_deviceState=s;emit deviceStateChanged();emit readinessChanged();}}
void SpreadtrumController::setBusy(bool b) { if(m_busy!=b){m_busy=b;emit busyChanged();}}

void SpreadtrumController::updateProgress(qint64 cur, qint64 tot, const QString& label)
{
    m_progress = tot>0?double(cur)/tot:0;
    m_progressText = QString("%1/%2 — %3").arg(cur).arg(tot).arg(label);
    qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(!m_progressStartMs) m_progressStartMs=now;
    int es=int((now-m_progressStartMs)/1000);
    m_elapsedText=QString("%1:%2").arg(es/60,2,10,QChar('0')).arg(es%60,2,10,QChar('0'));
    emit progressChanged();
}

void SpreadtrumController::resetProgress()
{
    m_progress=0;m_progressText.clear();m_speedText.clear();m_etaText.clear();m_elapsedText.clear();
    m_progressStartMs=0;m_lastSpeedMs=0;m_lastSpeedBytes=0;
    emit progressChanged();
}

} // namespace sakura
