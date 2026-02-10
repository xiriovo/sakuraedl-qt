#include "fastboot_controller.h"
#include "fastboot/services/fastboot_service.h"
#include "fastboot/parsers/payload_parser.h"
#include "core/logger.h"
#include <QtConcurrent>
#include <QTimerEvent>
#include <QTime>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QUrl>

namespace sakura {

FastbootController::FastbootController(QObject* parent)
    : QObject(parent)
    , m_service(std::make_unique<FastbootService>())
{
    // Wire service signals → controller
    QObject::connect(m_service.get(), &FastbootService::operationProgress,
                     this, [this](qint64 c, qint64 t) {
        updateProgress(c, t, m_progressText);
    });
    QObject::connect(m_service.get(), &FastbootService::operationInfo,
                     this, [this](const QString& msg) { addLog("  INFO: " + msg); });
    QObject::connect(m_service.get(), &FastbootService::deviceConnected,
                     this, [this](const QString& serial) {
        m_connected = true;
        stopAutoDetect();
        doRefreshInfo();
        addLogOk(L("Fastboot 已连接: ","Fastboot connected: ") + serial);
        emit connectionChanged();
    });
    QObject::connect(m_service.get(), &FastbootService::deviceDisconnected,
                     this, [this]() {
        m_connected = false;
        m_deviceInfo.clear();
        emit connectionChanged();
        emit deviceInfoChanged();
        // Auto-restart scanning when device disconnects
        startAutoDetect();
    });

    // Auto-start scanning on construction
    QMetaObject::invokeMethod(this, [this](){ startAutoDetect(); }, Qt::QueuedConnection);
}

FastbootController::~FastbootController() = default;

// ═══ i18n helpers ═══
void FastbootController::addLog(const QString& msg) { emit logMessage(QString("[%1] [FB] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void FastbootController::addLogOk(const QString& msg) { emit logMessage(QString("[%1] [FB] [OKAY] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void FastbootController::addLogErr(const QString& msg) { emit logMessage(QString("[%1] [FB] [ERROR] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
void FastbootController::addLogFail(const QString& msg) { emit logMessage(QString("[%1] [FB] [FAIL] %2").arg(QTime::currentTime().toString("HH:mm:ss"),msg)); }
QString FastbootController::L(const char* z, const char* e) const { return zh()?QString::fromUtf8(z):QString::fromUtf8(e); }

// ═══ STATUS ═══
QString FastbootController::statusHint() const
{
    if(m_connected) return L("已连接: ","Connected: ") + m_deviceInfo.value("serialno","?").toString();
    if(m_watching) return L("正在扫描 Fastboot 设备 (USB)...","Scanning for Fastboot devices (USB)...");
    return L("等待 Fastboot 设备连接...","Waiting for Fastboot device...");
}

// ═══ AUTO-DETECT ═══
void FastbootController::startAutoDetect()
{
    if(m_watching) return;
    m_watching = true;
    addLog(L("正在扫描 Fastboot USB 设备...","Scanning for Fastboot USB devices..."));
    if(!m_watchTimerId) m_watchTimerId = startTimer(1500);
    emit connectionChanged();
}

void FastbootController::stopAutoDetect()
{
    if(!m_watching) return;
    m_watching = false;
    if(m_watchTimerId) { killTimer(m_watchTimerId); m_watchTimerId=0; }
    emit connectionChanged();
}

void FastbootController::timerEvent(QTimerEvent* ev)
{
    if(ev->timerId() != m_watchTimerId) { QObject::timerEvent(ev); return; }
    // Use FastbootService to enumerate devices
    QStringList devices = m_service->detectDevices();
    if(!devices.isEmpty()) {
        addLogOk(L("发现 Fastboot 设备: ","Found Fastboot device: ") + devices.first());
        stopAutoDetect();
        doConnect(devices.first());
    }
}

// ═══ CONNECTION ═══
void FastbootController::doConnect(const QString& serial)
{
    if(m_busy) return;
    setBusy(true);
    addLog(L("正在连接 Fastboot 设备...","Connecting Fastboot device..."));

    (void)QtConcurrent::run([this, serial](){
        bool ok = m_service->selectDevice(serial);
        QMetaObject::invokeMethod(this, [this, ok](){
            if(ok) {
                m_connected = true;
                doRefreshInfo();
                setBusy(false);
                emit operationCompleted(true, L("已连接","Connected"));
            } else {
                addLogErr(L("连接失败","Connection failed"));
                setBusy(false);
                startAutoDetect(); // Resume scanning
            }
        }, Qt::QueuedConnection);
    });
}

void FastbootController::doRefreshInfo()
{
    auto info = m_service->refreshDeviceInfo();
    m_deviceInfo["serialno"] = info.serialNumber;
    m_deviceInfo["product"] = info.product;
    m_deviceInfo["version-bootloader"] = info.bootloaderVersion;
    m_deviceInfo["baseband"] = info.baseband;
    m_deviceInfo["hw-revision"] = info.hardwareRevision;
    m_deviceInfo["secure"] = info.secureState;
    m_deviceInfo["unlocked"] = info.isUnlocked;
    if(m_service->client()) m_maxDownload = m_service->client()->maxDownloadSize();

    // Read current slot
    if(m_service->client()) {
        QString slot = m_service->client()->getVariable("current-slot");
        if(!slot.isEmpty()) m_deviceInfo["current-slot"] = slot;
    }

    emit connectionChanged(); emit deviceInfoChanged();

    addLog(L("产品: ","Product: ") + info.product);
    addLog(L("序列号: ","Serial: ") + info.serialNumber);
    addLog(L("Bootloader: ","Bootloader: ") + info.bootloaderVersion);
    addLog(L("安全: ","Secure: ") + info.secureState +
           L(" | 解锁: "," | Unlocked: ") + (info.isUnlocked?"yes":"no"));
}

void FastbootController::disconnect()
{
    stopAutoDetect();
    m_service->disconnect();
    addLog(L("已断开连接","Disconnected"));
    m_connected = false;
    m_deviceInfo.clear();
    emit connectionChanged(); emit deviceInfoChanged();
}

void FastbootController::stopOperation()
{
    addLog(L("操作已取消","Operation cancelled"));
    stopAutoDetect(); resetProgress(); setBusy(false);
}

// ═══ FILE LOADING ═══
void FastbootController::loadImages(const QStringList& paths)
{
    m_partitions.clear();
    for(const auto& p : paths) {
        QFileInfo fi(p);
        QString name = fi.completeBaseName();
        QVariantMap part;
        part["name"] = name; part["filePath"] = p; part["size"] = fi.size();
        part["sizeStr"] = QString("%1 MB").arg(fi.size()/1048576.0, 0, 'f', 1);
        part["checked"] = true;
        m_partitions.append(part);
    }
    m_checkedCount = m_partitions.size();
    addLogOk(L("已加载 ","Loaded ") + QString::number(paths.size()) + L(" 个镜像文件"," image files"));
    for(const auto& p : paths) addLog("  → " + QFileInfo(p).fileName());
    emit partitionsChanged();
}

void FastbootController::loadFirmwareDir(const QString& dirPath)
{
    QDir dir(dirPath);
    QStringList imgs = dir.entryList({"*.img","*.bin","*.mbn"}, QDir::Files);
    if(imgs.isEmpty()) { addLogErr(L("未找到镜像文件","No image files found")); return; }
    QStringList paths;
    for(const auto& f : imgs) paths << dir.absoluteFilePath(f);
    loadImages(paths);
}

void FastbootController::loadPayload(const QString& path)
{
    if(path.isEmpty()) return;
    m_payloadPath = path;
    addLog(L("正在解析 Payload.bin...","Parsing Payload.bin..."));

    m_payload = std::make_unique<PayloadParser>();
    if(!m_payload->load(path)) {
        addLogErr(L("Payload.bin 解析失败","Failed to parse Payload.bin"));
        m_payload.reset();
        return;
    }

    m_payloadLoaded = true;
    m_partitions.clear();
    auto names = m_payload->partitionNames();
    for(const auto& n : names) {
        QVariantMap p;
        p["name"] = n;
        auto* part = m_payload->partition(n);
        p["size"] = part ? qint64(part->size) : 0;
        p["sizeStr"] = part ? QString("%1 MB").arg(part->size/1048576.0, 0, 'f', 1) : "(payload)";
        p["checked"] = true;
        p["fromPayload"] = true;
        m_partitions.append(p);
    }
    m_checkedCount = m_partitions.size();
    addLogOk(L("Payload 已加载: ","Payload loaded: ") + QString::number(names.size()) + L(" 个分区"," partitions"));
    emit partitionsChanged(); emit payloadChanged();
}

// ═══ FLASH OPERATIONS ═══
void FastbootController::flashAll()
{
    if(!m_connected) { addLogErr(L("需要 Fastboot 设备连接","Fastboot device must be connected")); return; }
    if(!hasCheckedPartitions()) { addLogFail(L("未选择分区","No partitions selected")); return; }
    setBusy(true);
    QVariantList checked;
    for(const auto& v : m_partitions) if(v.toMap()["checked"].toBool()) checked.append(v);
    addLog(L("正在刷写 ","Flashing ") + QString::number(checked.size()) + L(" 个分区...","partitions..."));

    (void)QtConcurrent::run([this,checked](){
        int ok=0, fail=0;
        for(int i=0;i<checked.size();i++){
            QVariantMap info=checked[i].toMap();
            QString name=info["name"].toString();
            QString file=info["filePath"].toString();
            bool isPayload=info["fromPayload"].toBool();

            QMetaObject::invokeMethod(this,[this,name,i,checked](){
                addLog(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + L("刷写 ","Flashing ") + name + "...");
                updateProgress(i, checked.size(), name);
            },Qt::QueuedConnection);

            bool success = false;
            if(isPayload && m_payload) {
                // Extract partition from payload and flash
                QString tmpPath = QDir::temp().filePath("sakura_fb_" + name + ".img");
                if(m_payload->extractPartition(name, tmpPath, [this,i,checked](qint64 c, qint64 t){
                    QMetaObject::invokeMethod(this,[this,c,t](){ updateProgress(c,t,""); },Qt::QueuedConnection);
                })) {
                    success = m_service->flashPartition(name, tmpPath);
                    QFile::remove(tmpPath);
                }
            } else if(!file.isEmpty()) {
                success = m_service->flashPartition(name, file);
            }

            QMetaObject::invokeMethod(this,[this,name,i,checked,success](){
                if(success) {
                    addLogOk(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → OKAY");
                    updateProgress(i+1, checked.size(), name);
                } else {
                    addLogFail(QString("  [%1/%2] ").arg(i+1).arg(checked.size()) + name + " → FAIL");
                }
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,ok,fail,checked](){
            if(fail==0)
                addLogOk(L("刷写完成: ","Flash complete: ") + QString::number(ok) + L(" 个分区"," partitions"));
            else
                addLogErr(L("刷写完成: ","Flash complete: ") + QString::number(ok) + L(" 成功, "," OK, ") + QString::number(fail) + L(" 失败"," failed"));
            resetProgress(); setBusy(false);
        });
    });
}

void FastbootController::flashPartition(const QString& name, const QString& filePath)
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    setBusy(true);
    addLog(L("正在刷写 ","Flashing ") + name + " ← " + QFileInfo(filePath).fileName());

    (void)QtConcurrent::run([this,name,filePath](){
        bool ok = m_service->flashPartition(name, filePath);
        QMetaObject::invokeMethod(this,[this,name,ok](){
            if(ok) addLogOk(name + " → OKAY");
            else   addLogFail(name + " → FAIL");
            setBusy(false);
        },Qt::QueuedConnection);
    });
}

void FastbootController::erasePartition(const QString& name)
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLog(L("正在擦除 ","Erasing ") + name);

    (void)QtConcurrent::run([this,name](){
        bool ok = m_service->erasePartition(name);
        QMetaObject::invokeMethod(this,[this,name,ok](){
            if(ok) addLogOk(name + " → erased");
            else   addLogFail(name + " → erase FAIL");
        },Qt::QueuedConnection);
    });
}

void FastbootController::eraseUserdata() { erasePartition("userdata"); }

// ═══ DEVICE CONTROL ═══
void FastbootController::reboot()
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLogOk(L("重启设备","Rebooting device"));
    m_service->reboot();
    m_connected=false; emit connectionChanged();
}

void FastbootController::rebootBootloader()
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLogOk(L("重启到 Bootloader","Rebooting to bootloader"));
    m_service->rebootBootloader();
}

void FastbootController::rebootRecovery()
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLogOk(L("重启到 Recovery","Rebooting to recovery"));
    m_service->rebootRecovery();
}

void FastbootController::rebootFastbootd()
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    addLogOk(L("重启到 Fastbootd","Rebooting to fastbootd"));
    m_service->client()->rebootFastbootd();
}

// ═══ UNLOCK / LOCK ═══
void FastbootController::unlockBootloader()
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLog(L("正在解锁 Bootloader...","Unlocking bootloader..."));
    bool ok = m_service->oemUnlock();
    if(ok) addLogOk(L("Bootloader 已解锁","Bootloader unlocked"));
    else   addLogFail(L("Bootloader 解锁失败","Bootloader unlock failed"));
}

void FastbootController::lockBootloader()
{
    if(!m_connected) { addLogErr(L("未连接","Not connected")); return; }
    addLog(L("正在锁定 Bootloader...","Locking bootloader..."));
    bool ok = m_service->oemLock();
    if(ok) addLogOk(L("Bootloader 已锁定","Bootloader locked"));
    else   addLogFail(L("Bootloader 锁定失败","Bootloader lock failed"));
}

// ═══ A/B SLOTS ═══
void FastbootController::setActiveSlot(const QString& slot)
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    addLog(L("设置活动槽位: ","Setting active slot: ") + slot);
    auto resp = m_service->client()->sendCommand("set_active:" + slot);
    if(resp.isOkay()) {
        m_deviceInfo["current-slot"] = slot;
        emit deviceInfoChanged();
        addLogOk(L("活动槽位: ","Active slot: ") + slot);
    } else {
        addLogFail(L("设置槽位失败: ","Set slot failed: ") + resp.data);
    }
}

void FastbootController::getVariable(const QString& name)
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    QString val = m_service->client()->getVariable(name);
    addLog("getvar:" + name + " = " + val);
    emit variableResult(name, val);
}

// ═══ OEM ═══
void FastbootController::oemCommand(const QString& cmd)
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    addLog("oem " + cmd);
    auto resp = m_service->client()->oemCommand(cmd);
    if(resp) addLogOk("oem " + cmd + " → OKAY");
    else     addLogFail("oem " + cmd + " → FAIL");
}

// ═══ READ PARTITION TABLE ═══
void FastbootController::readPartitionTable()
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    setBusy(true);
    addLog(L("正在读取分区表...","Reading partition table..."));

    m_partitions.clear();
    m_checkedCount = 0;
    emit partitionsChanged();

    (void)QtConcurrent::run([this](){
        // Use getvar:all to enumerate partitions
        QStringList partNames;
        QString allVars = m_service->client()->getVariable("all");
        if(!allVars.isEmpty()) {
            // Parse partition-type:xxx and partition-size:xxx entries
            QStringList lines = allVars.split('\n', Qt::SkipEmptyParts);
            for(const auto& line : lines) {
                if(line.contains("partition-size:")) {
                    // format: (bootloader) partition-size:boot: 0x4000000
                    QString trimmed = line.trimmed();
                    int idx = trimmed.indexOf("partition-size:");
                    if(idx >= 0) {
                        QString rest = trimmed.mid(idx + 15);
                        int colon = rest.indexOf(':');
                        if(colon > 0) {
                            QString pname = rest.left(colon).trimmed();
                            QString psize = rest.mid(colon+1).trimmed();
                            if(!partNames.contains(pname)) {
                                partNames.append(pname);
                                QVariantMap p;
                                p["name"] = pname;
                                bool ok;
                                qint64 bytes = psize.toLongLong(&ok, 0);
                                p["size"] = bytes;
                                p["sizeStr"] = ok ? QString("%1 MB").arg(bytes/1048576.0, 0, 'f', 1) : psize;
                                p["checked"] = false;
                                p["filePath"] = "";
                                QMetaObject::invokeMethod(this,[this,p](){
                                    m_partitions.append(p);
                                },Qt::QueuedConnection);
                            }
                        }
                    }
                }
            }
        }

        // Fallback: try common partition names
        if(partNames.isEmpty()) {
            QStringList common = {"boot","recovery","system","vendor","userdata","cache","dtbo",
                                  "vbmeta","super","modem","metadata","misc","logo","splash"};
            for(const auto& name : common) {
                QString sz = m_service->client()->getVariable("partition-size:" + name);
                if(!sz.isEmpty() && sz != "(null)") {
                    partNames.append(name);
                    QVariantMap p;
                    p["name"] = name;
                    bool ok;
                    qint64 bytes = sz.toLongLong(&ok, 0);
                    p["size"] = bytes;
                    p["sizeStr"] = ok ? QString("%1 MB").arg(bytes/1048576.0, 0, 'f', 1) : sz;
                    p["checked"] = false;
                    p["filePath"] = "";
                    QMetaObject::invokeMethod(this,[this,p](){
                        m_partitions.append(p);
                    },Qt::QueuedConnection);
                }
            }
        }

        QMetaObject::invokeMethod(this,[this,partNames](){
            if(!partNames.isEmpty()) {
                m_checkedCount = 0;
                addLogOk(L("分区表已读取: ","Partition table read: ") + QString::number(partNames.size()) + L(" 个分区"," partitions"));
                for(const auto& n : partNames) addLog("  • " + n);
            } else {
                addLogErr(L("无法读取分区表","Failed to read partition table"));
            }
            emit partitionsChanged();
            setBusy(false);
        },Qt::QueuedConnection);
    });
}

// ═══ CUSTOM COMMAND ═══
void FastbootController::customCommand(const QString& cmd)
{
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    if(cmd.trimmed().isEmpty()) return;
    addLog(L("执行自定义命令: ","Executing custom command: ") + cmd);

    // Parse the command
    QStringList parts = cmd.trimmed().split(' ', Qt::SkipEmptyParts);
    QString first = parts[0].toLower();

    // Strip leading "fastboot" if present
    if(first == "fastboot" && parts.size() > 1) { parts.removeFirst(); first = parts[0].toLower(); }

    bool success = false;
    QString result;

    if(first == "getvar" && parts.size() >= 2) {
        result = m_service->client()->getVariable(parts[1]);
        success = !result.isEmpty();
        if(success) addLogOk(parts[1] + " = " + result);
        else addLogFail("getvar " + parts[1] + " → empty");
    } else if(first == "oem" && parts.size() >= 2) {
        auto r = m_service->client()->oemCommand(parts.mid(1).join(' '));
        success = r;
        if(success) addLogOk("oem → OKAY");
        else addLogFail("oem → FAIL");
    } else if(first == "erase" && parts.size() >= 2) {
        success = m_service->erasePartition(parts[1]);
        if(success) addLogOk("erase " + parts[1] + " → OKAY");
        else addLogFail("erase " + parts[1] + " → FAIL");
    } else if(first == "flash" && parts.size() >= 3) {
        success = m_service->flashPartition(parts[1], parts[2]);
        if(success) addLogOk("flash " + parts[1] + " → OKAY");
        else addLogFail("flash " + parts[1] + " → FAIL");
    } else if(first == "reboot") {
        success = m_service->reboot();
        if(success) addLogOk("reboot → OKAY");
    } else {
        // Send raw command
        auto resp = m_service->client()->sendCommand(cmd.trimmed());
        success = resp.isOkay();
        if(success) addLogOk(cmd + " → " + resp.data);
        else addLogFail(cmd + " → " + resp.data);
    }
}

// ═══ CLOUD URL PARSE ═══
void FastbootController::parseCloudUrl(const QString& url)
{
    if(url.trimmed().isEmpty()) { addLogErr(L("URL 为空","URL is empty")); return; }
    addLog(L("正在解析云端固件 URL...","Parsing cloud firmware URL..."));
    addLog("  URL: " + url);

    // Parse URL to extract firmware info
    // Support common OTA/firmware URL patterns
    // e.g., https://xxx.com/firmware/device/version/images.zip
    QUrl parsedUrl(url.trimmed());
    if(!parsedUrl.isValid()) {
        addLogErr(L("无效的 URL","Invalid URL"));
        return;
    }

    QString path = parsedUrl.path();
    QString host = parsedUrl.host();
    addLog(L("  主机: ","  Host: ") + host);
    addLog(L("  路径: ","  Path: ") + path);

    // Extract firmware metadata from URL path
    QStringList segments = path.split('/', Qt::SkipEmptyParts);
    if(!segments.isEmpty()) {
        addLog(L("  固件标识: ","  Firmware ID: ") + segments.last());
    }

    addLogOk(L("URL 解析完成，请使用下载工具获取固件后加载","URL parsed. Download firmware externally then load"));
}

// ═══ PAYLOAD ═══
void FastbootController::extractPayloadPartition(const QString& name, const QString& savePath)
{
    if(!m_payloadLoaded || !m_payload) { addLogErr(L("未加载 payload","No payload loaded")); return; }
    setBusy(true);
    addLog(L("正在提取 ","Extracting ") + name + " → " + savePath);
    (void)QtConcurrent::run([this,name,savePath](){
        bool ok = m_payload->extractPartition(name, savePath, [this](qint64 c, qint64 t){
            QMetaObject::invokeMethod(this,[this,c,t](){ updateProgress(c,t,""); },Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(this,[this,name,ok](){
            if(ok) addLogOk(name + L(" 提取完成"," extracted"));
            else   addLogFail(name + L(" 提取失败"," extract failed"));
            resetProgress(); setBusy(false);
        });
    });
}

// ═══ SCRIPT ═══
void FastbootController::loadBatScript(const QString& path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) { addLogErr(L("无法打开脚本","Cannot open script")); return; }
    m_batScript.clear();
    while(!f.atEnd()) {
        QString line = f.readLine().trimmed();
        if(!line.isEmpty() && !line.startsWith('#') && !line.startsWith("REM"))
            m_batScript.append(line);
    }
    addLogOk(L("加载脚本: ","Loaded script: ") + QFileInfo(path).fileName() +
             " (" + QString::number(m_batScript.size()) + L(" 行"," lines") + ")");
}

void FastbootController::executeBatScript()
{
    if(m_batScript.isEmpty()) { addLogErr(L("无脚本","No script loaded")); return; }
    if(!m_connected || !m_service->client()) { addLogErr(L("未连接","Not connected")); return; }
    setBusy(true);
    addLog(L("正在执行脚本...","Executing script..."));

    QStringList script = m_batScript;
    (void)QtConcurrent::run([this,script](){
        int ok=0, fail=0;
        for(int i=0;i<script.size();i++){
            QString line = script[i];
            QMetaObject::invokeMethod(this,[this,line,i,script](){
                addLog(QString("  [%1/%2] %3").arg(i+1).arg(script.size()).arg(line));
            },Qt::QueuedConnection);

            // Parse fastboot command
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if(parts.isEmpty()) continue;
            QString cmd = parts[0].toLower();
            if(cmd == "fastboot" && parts.size()>1) { parts.removeFirst(); cmd = parts[0].toLower(); }

            bool success = false;
            if(cmd == "flash" && parts.size()>=3) {
                success = m_service->flashPartition(parts[1], parts[2]);
            } else if(cmd == "erase" && parts.size()>=2) {
                success = m_service->erasePartition(parts[1]);
            } else if(cmd == "reboot") {
                success = m_service->reboot();
            } else if(cmd == "reboot-bootloader") {
                success = m_service->rebootBootloader();
            } else if(cmd == "oem" && parts.size()>=2) {
                auto r = m_service->client()->oemCommand(parts.mid(1).join(' '));
                success = r;
            } else {
                auto r = m_service->client()->sendCommand(line);
                success = r.isOkay();
            }

            QMetaObject::invokeMethod(this,[this,success](){
                if(success) addLogOk("  → OKAY"); else addLogFail("  → FAIL");
            },Qt::QueuedConnection);
            if(success) ok++; else fail++;
        }
        QMetaObject::invokeMethod(this,[this,ok,fail](){
            addLogOk(L("脚本执行完成: ","Script complete: ") + QString::number(ok) + " OK, " + QString::number(fail) + " FAIL");
            resetProgress(); setBusy(false);
        });
    });
}

// ═══ PARTITION MANAGEMENT ═══
void FastbootController::togglePartition(int index)
{
    if(index<0||index>=m_partitions.size()) return;
    QVariantMap p=m_partitions[index].toMap();
    p["checked"]=!p["checked"].toBool(); m_partitions[index]=p;
    m_checkedCount=0; for(const auto& v:m_partitions) if(v.toMap()["checked"].toBool()) m_checkedCount++;
    emit partitionsChanged();
}

void FastbootController::selectAll(bool checked)
{
    for(int i=0;i<m_partitions.size();i++){QVariantMap p=m_partitions[i].toMap();p["checked"]=checked;m_partitions[i]=p;}
    m_checkedCount=checked?m_partitions.size():0;
    emit partitionsChanged();
}

// ═══ HELPERS ═══
void FastbootController::setBusy(bool b) { if(m_busy!=b){m_busy=b;emit busyChanged();}}

void FastbootController::updateProgress(qint64 cur, qint64 tot, const QString& label)
{
    m_progress = tot>0?double(cur)/tot:0;
    if(!label.isEmpty()) m_progressText = QString("%1/%2 — %3").arg(cur).arg(tot).arg(label);
    qint64 now=QDateTime::currentMSecsSinceEpoch();
    if(!m_progressStartMs) m_progressStartMs=now;
    int es=int((now-m_progressStartMs)/1000);
    m_elapsedText=QString("%1:%2").arg(es/60,2,10,QChar('0')).arg(es%60,2,10,QChar('0'));
    emit progressChanged();
}

void FastbootController::resetProgress()
{
    m_progress=0;m_progressText.clear();m_speedText.clear();m_etaText.clear();m_elapsedText.clear();
    m_progressStartMs=0;m_lastSpeedMs=0;m_lastSpeedBytes=0;
    emit progressChanged();
}

} // namespace sakura
