#include "port_detector.h"
#include "core/logger.h"
#include <QTimer>
#include <QRegularExpression>

#ifndef _WIN32
#include <QSerialPortInfo>
#endif

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>

// Device Manager GUIDs for multiple device classes
// GUID_DEVCLASS_PORTS = {4D36E978-E325-11CE-BFC1-08002BE10318} — COM & LPT
static const GUID GUID_CLASS_SERIAL_PORTS =
    { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
// GUID_DEVCLASS_USB = {36FC9E60-C465-11CF-8056-444553540000} — USB controllers/hubs
static const GUID GUID_CLASS_USB =
    { 0x36FC9E60, 0xC465, 0x11CF, { 0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
// GUID_DEVCLASS_USBDEVICE = {88BAE032-5A81-49F0-BC3D-A4FF138216D6} — USB devices
static const GUID GUID_CLASS_USBDEVICE =
    { 0x88BAE032, 0x5A81, 0x49F0, { 0xBC, 0x3D, 0xA4, 0xFF, 0x13, 0x82, 0x16, 0xD6 } };
// GUID_DEVCLASS_MODEM = {4D36E96D-E325-11CE-BFC1-08002BE10318} — Modems
static const GUID GUID_CLASS_MODEM =
    { 0x4D36E96D, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
// GUID_DEVCLASS_UNKNOWN = {4D36E97E-E325-11CE-BFC1-08002BE10318} — Unknown/Other devices
static const GUID GUID_CLASS_UNKNOWN =
    { 0x4D36E97E, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };
// GUID_DEVCLASS_WPD = {EEC5AD98-8080-425F-922A-DABF3DE3F69A} — WPD (MTP/PTP)
static const GUID GUID_CLASS_WPD =
    { 0xEEC5AD98, 0x8080, 0x425F, { 0x92, 0x2A, 0xDA, 0xBF, 0x3D, 0xE3, 0xF6, 0x9A } };
// GUID_DEVCLASS_ANDROIDUSB = {3F966BD9-FA04-4EC5-991C-D326973B5128} — Android USB
static const GUID GUID_CLASS_ANDROIDUSB =
    { 0x3F966BD9, 0xFA04, 0x4EC5, { 0x99, 0x1C, 0xD3, 0x26, 0x97, 0x3B, 0x51, 0x28 } };
#endif

namespace sakura {

static const char* LOG_TAG = "PortDetector";

PortDetector::PortDetector(QObject* parent) : QObject(parent) {}

PortType PortDetector::identifyPortType(uint16_t vid, uint16_t pid)
{
    // Qualcomm
    if (vid == 0x05C6) {
        if (pid == 0x9008) return PortType::QualcommEdl9008;
        if (pid == 0x9006) return PortType::QualcommDload9006;
        if (pid == 0x9091 || pid == 0x901D) return PortType::QualcommDiag9091;
    }

    // MediaTek (based on MTK META UTILITY V48)
    if (vid == 0x0E8D || vid == 0x1004 || vid == 0x22D9 || vid == 0x2717 || vid == 0x2A45 || vid == 0x0B05) {
        // BROM mode (Boot ROM)
        if (pid == 0x0003 || pid == 0x0002) return PortType::MtkBrom;
        // Preloader mode
        if (pid == 0x2000 || pid == 0x6000 || pid == 0x0616) return PortType::MtkPreloader;
        // DA mode (Download Agent)
        if (pid == 0x2001 || pid == 0x2003 || pid == 0x2004 || pid == 0x2005
            || pid == 0x00A5 || pid == 0x00A2) return PortType::MtkDa;
    }

    // Spreadtrum
    if (vid == 0x1782) {
        if (pid == 0x4D00 || pid == 0x4D12) return PortType::SpreadtrumDownload;
    }

    // Google Fastboot
    if ((vid == 0x18D1 && pid == 0xD00D) || (vid == 0x18D1 && pid == 0x4EE0))
        return PortType::Fastboot;

    // Generic Fastboot
    if (pid == 0xD00D) return PortType::Fastboot;

    return PortType::Unknown;
}

// ═══ Win32 Native Device Manager Enumeration ═══
#ifdef _WIN32

/**
 * Parse VID and PID from a device instance ID or hardware ID string.
 * Handles formats:
 *   "USB\VID_0E8D&PID_0003\..."
 *   "USB\VID_0E8D&PID_0003&MI_01\..."
 */
static bool parseVidPid(const QString& instanceId, uint16_t& vid, uint16_t& pid)
{
    static QRegularExpression vidRe("VID_(\\w{4})", QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression pidRe("PID_(\\w{4})", QRegularExpression::CaseInsensitiveOption);

    auto vidMatch = vidRe.match(instanceId);
    auto pidMatch = pidRe.match(instanceId);

    if (vidMatch.hasMatch() && pidMatch.hasMatch()) {
        bool okV, okP;
        vid = vidMatch.captured(1).toUShort(&okV, 16);
        pid = pidMatch.captured(1).toUShort(&okP, 16);
        return okV && okP;
    }
    return false;
}

/**
 * Check if a VID belongs to a known vendor we care about.
 */
static bool isKnownVendor(uint16_t vid)
{
    switch (vid) {
    case 0x0E8D: return true;   // MediaTek
    case 0x1004: return true;   // LG (MTK)
    case 0x22D9: return true;   // OPPO (MTK)
    case 0x2717: return true;   // Xiaomi (MTK)
    case 0x2A45: return true;   // Meizu (MTK)
    case 0x0B05: return true;   // ASUS (MTK)
    case 0x05C6: return true;   // Qualcomm
    case 0x1782: return true;   // Spreadtrum
    case 0x18D1: return true;   // Google
    case 0x2C7C: return true;   // Quectel (Qualcomm based modems)
    case 0x1BBB: return true;   // T&A Mobile (Alcatel)
    case 0x0FCE: return true;   // Sony (some MTK)
    case 0x0BB4: return true;   // HTC
    case 0x04E8: return true;   // Samsung
    case 0x2A96: return true;   // Coolpad
    default: return false;
    }
}

/**
 * Get a string property from a SetupDi device info.
 */
static QString getDeviceStringProperty(HDEVINFO hDevInfo, PSP_DEVINFO_DATA devInfo, DWORD prop)
{
    wchar_t buf[512];
    if (SetupDiGetDeviceRegistryPropertyW(
            hDevInfo, devInfo, prop, nullptr,
            reinterpret_cast<PBYTE>(buf), sizeof(buf), nullptr)) {
        return QString::fromWCharArray(buf);
    }
    return {};
}

/**
 * Try to get COM port name from device registry key.
 */
static QString getComPortName(HDEVINFO hDevInfo, PSP_DEVINFO_DATA devInfo)
{
    HKEY hKey = SetupDiOpenDevRegKey(
        hDevInfo, devInfo,
        DICS_FLAG_GLOBAL, 0,
        DIREG_DEV, KEY_READ
    );
    if (hKey == INVALID_HANDLE_VALUE) return {};

    wchar_t portName[256];
    DWORD portNameSize = sizeof(portName);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hKey, L"PortName", nullptr, &type,
                               reinterpret_cast<LPBYTE>(portName), &portNameSize);
    RegCloseKey(hKey);

    if (r == ERROR_SUCCESS && type == REG_SZ) {
        QString name = QString::fromWCharArray(portName);
        if (name.startsWith("COM", Qt::CaseInsensitive))
            return name;
    }
    return {};
}

/**
 * Scan one Device Manager class for devices matching known VIDs.
 */
static void scanDeviceClass(const GUID* classGuid, const QString& className,
                            QList<sakura::DetectedPort>& result)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        classGuid, nullptr, nullptr, DIGCF_PRESENT
    );
    if (hDevInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        // Get hardware ID for VID/PID extraction
        uint16_t vid = 0, pid = 0;
        wchar_t hwIdBuf[1024];
        if (SetupDiGetDeviceRegistryPropertyW(
                hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hwIdBuf), sizeof(hwIdBuf), nullptr)) {
            QString hwId = QString::fromWCharArray(hwIdBuf);
            parseVidPid(hwId, vid, pid);
        }

        // Also try device instance ID
        wchar_t instanceBuf[512];
        QString instanceId;
        if (SetupDiGetDeviceInstanceIdW(
                hDevInfo, &devInfoData,
                instanceBuf, sizeof(instanceBuf) / sizeof(wchar_t), nullptr)) {
            instanceId = QString::fromWCharArray(instanceBuf);
            if (vid == 0 || pid == 0)
                parseVidPid(instanceId, vid, pid);
        }

        // Only include devices with known vendor IDs
        if (!isKnownVendor(vid)) continue;

        // Check if this instance is already in the result
        bool duplicate = false;
        for (const auto& existing : result) {
            if (!instanceId.isEmpty() && existing.instanceId == instanceId) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        sakura::DetectedPort port;
        port.vid = vid;
        port.pid = pid;
        port.instanceId = instanceId;
        port.deviceClass = className;

        // Get device description and friendly name
        port.description = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        port.friendlyName = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        port.driver = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_DRIVER);

        // Try to get COM port name
        QString comName = getComPortName(hDevInfo, &devInfoData);
        if (!comName.isEmpty()) {
            port.portName = comName;
            port.hasComPort = true;
        } else {
            // No COM port — use instance ID as identifier
            port.portName = instanceId;
            port.hasComPort = false;
            port.isUsb = true;
        }

        // Get device status from Configuration Manager
        DEVINST devInst = devInfoData.DevInst;
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, devInst, 0) == CR_SUCCESS) {
            port.devStatus = status;
            port.devProblem = problem;
        }

        // Classify
        port.deviceId = QString("%1:%2")
                            .arg(port.vid, 4, 16, QChar('0'))
                            .arg(port.pid, 4, 16, QChar('0'));
        port.type = sakura::PortDetector::identifyPortType(port.vid, port.pid);
        port.isEdl = (port.type == sakura::PortType::QualcommEdl9008 ||
                      port.type == sakura::PortType::QualcommDload9006);
        port.isMtk = (port.type == sakura::PortType::MtkPreloader ||
                      port.type == sakura::PortType::MtkBrom ||
                      port.type == sakura::PortType::MtkDa);
        port.isSprd = (port.type == sakura::PortType::SpreadtrumDownload);
        port.isFastboot = (port.type == sakura::PortType::Fastboot);

        result.append(port);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
}

QList<DetectedPort> PortDetector::enumerateComPortsNative()
{
    QList<DetectedPort> result;
    scanDeviceClass(&GUID_CLASS_SERIAL_PORTS, "Ports", result);

    // Filter to only COM ports for this method
    QList<DetectedPort> comOnly;
    for (const auto& p : result) {
        if (p.hasComPort)
            comOnly.append(p);
    }

    LOG_INFO_CAT(LOG_TAG, QString("Native COM enumeration: found %1 ports").arg(comOnly.size()));
    return comOnly;
}

QList<DetectedPort> PortDetector::enumerateDeviceManager()
{
    QList<DetectedPort> result;

    // Scan all relevant Device Manager classes
    // Order: Ports first (most likely to have COM), then USB, then others
    scanDeviceClass(&GUID_CLASS_SERIAL_PORTS, "Ports",       result);
    scanDeviceClass(&GUID_CLASS_USB,          "USB",         result);
    scanDeviceClass(&GUID_CLASS_USBDEVICE,    "USBDevice",   result);
    scanDeviceClass(&GUID_CLASS_MODEM,        "Modem",       result);
    scanDeviceClass(&GUID_CLASS_UNKNOWN,      "Unknown",     result);
    scanDeviceClass(&GUID_CLASS_WPD,          "WPD",         result);
    scanDeviceClass(&GUID_CLASS_ANDROIDUSB,   "AndroidUSB",  result);

    // Also scan ALL present USB devices (catch-all for any class)
    // Use null GUID + DIGCF_ALLCLASSES to find devices in any class
    {
        HDEVINFO hDevInfo = SetupDiGetClassDevsW(
            nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES
        );
        if (hDevInfo != INVALID_HANDLE_VALUE) {
            SP_DEVINFO_DATA devInfoData;
            devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

            for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
                uint16_t vid = 0, pid = 0;
                wchar_t hwIdBuf[1024];
                if (SetupDiGetDeviceRegistryPropertyW(
                        hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr,
                        reinterpret_cast<PBYTE>(hwIdBuf), sizeof(hwIdBuf), nullptr)) {
                    parseVidPid(QString::fromWCharArray(hwIdBuf), vid, pid);
                }

                wchar_t instanceBuf[512];
                QString instanceId;
                if (SetupDiGetDeviceInstanceIdW(
                        hDevInfo, &devInfoData,
                        instanceBuf, sizeof(instanceBuf) / sizeof(wchar_t), nullptr)) {
                    instanceId = QString::fromWCharArray(instanceBuf);
                    if (vid == 0 || pid == 0)
                        parseVidPid(instanceId, vid, pid);
                }

                if (!isKnownVendor(vid)) continue;

                // Skip duplicates
                bool duplicate = false;
                for (const auto& existing : result) {
                    if (!instanceId.isEmpty() && existing.instanceId == instanceId) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                DetectedPort port;
                port.vid = vid;
                port.pid = pid;
                port.instanceId = instanceId;
                port.description = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
                port.friendlyName = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
                port.driver = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_DRIVER);
                port.deviceClass = getDeviceStringProperty(hDevInfo, &devInfoData, SPDRP_CLASS);
                if (port.deviceClass.isEmpty())
                    port.deviceClass = "AllUSB";

                QString comName = getComPortName(hDevInfo, &devInfoData);
                if (!comName.isEmpty()) {
                    port.portName = comName;
                    port.hasComPort = true;
                } else {
                    port.portName = instanceId;
                    port.hasComPort = false;
                    port.isUsb = true;
                }

                DEVINST devInst = devInfoData.DevInst;
                ULONG status = 0, problem = 0;
                if (CM_Get_DevNode_Status(&status, &problem, devInst, 0) == CR_SUCCESS) {
                    port.devStatus = status;
                    port.devProblem = problem;
                }

                port.deviceId = QString("%1:%2").arg(vid, 4, 16, QChar('0')).arg(pid, 4, 16, QChar('0'));
                port.type = identifyPortType(vid, pid);
                port.isEdl = (port.type == PortType::QualcommEdl9008 || port.type == PortType::QualcommDload9006);
                port.isMtk = (port.type == PortType::MtkPreloader || port.type == PortType::MtkBrom || port.type == PortType::MtkDa);
                port.isSprd = (port.type == PortType::SpreadtrumDownload);
                port.isFastboot = (port.type == PortType::Fastboot);

                result.append(port);
            }

            SetupDiDestroyDeviceInfoList(hDevInfo);
        }
    }

    LOG_INFO_CAT(LOG_TAG, QString("Device Manager scan: found %1 known-vendor devices").arg(result.size()));
    return result;
}

#else
// Non-Windows stubs
QList<DetectedPort> PortDetector::enumerateComPortsNative()
{
    return detectAllPorts();
}

QList<DetectedPort> PortDetector::enumerateDeviceManager()
{
    return detectAllPorts();
}
#endif

// ═══ Detection Methods ═══

QList<DetectedPort> PortDetector::detectAllPorts()
{
    QList<DetectedPort> result;

#ifdef _WIN32
    // Full Device Manager scan: Ports + USB + USBDevice + Modem + Unknown + WPD + AndroidUSB
    // This catches devices even before VCOM driver loads
    result = enumerateDeviceManager();
#else
    // Fallback: use QSerialPortInfo on non-Windows platforms
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        DetectedPort port;
        port.portName = info.portName();
        port.description = info.description();
        port.vid = info.vendorIdentifier();
        port.pid = info.productIdentifier();
        port.deviceId = QString("%1:%2").arg(port.vid, 4, 16, QChar('0')).arg(port.pid, 4, 16, QChar('0'));
        port.type = identifyPortType(port.vid, port.pid);
        port.isEdl = (port.type == PortType::QualcommEdl9008 || port.type == PortType::QualcommDload9006);
        port.isMtk = (port.type == PortType::MtkPreloader || port.type == PortType::MtkBrom || port.type == PortType::MtkDa);
        port.isSprd = (port.type == PortType::SpreadtrumDownload);
        port.hasComPort = true;
        result.append(port);
    }
#endif

    // NOTE: libusb is NOT used for device detection.
    // libusb is only for BROM exploits (kamakiri, etc.) and Fastboot.
    // Device detection relies entirely on Device Manager (SetupDi) + CreateFileA.
    // Using libusb for detection can interfere with VCOM drivers and cause
    // device locking that requires a system restart.

    return result;
}

QList<DetectedPort> PortDetector::detectEdlPorts()
{
    QList<DetectedPort> result;
    for (const auto& p : detectAllPorts()) {
        if (p.isEdl) result.append(p);
    }
    return result;
}

QList<DetectedPort> PortDetector::detectMtkPorts()
{
    QList<DetectedPort> result;
    for (const auto& p : detectAllPorts()) {
        if (p.isMtk) result.append(p);
    }
    return result;
}

QList<DetectedPort> PortDetector::detectSprdPorts()
{
    QList<DetectedPort> result;
    for (const auto& p : detectAllPorts()) {
        if (p.isSprd) result.append(p);
    }
    return result;
}

QList<DetectedPort> PortDetector::detectFastbootDevices()
{
    QList<DetectedPort> result;
    for (const auto& p : detectAllPorts()) {
        if (p.isFastboot) result.append(p);
    }
    return result;
}

DetectedPort PortDetector::getFirstEdlPort()
{
    auto ports = detectEdlPorts();
    return ports.isEmpty() ? DetectedPort{} : ports.first();
}

DetectedPort PortDetector::getFirstMtkPort()
{
    auto ports = detectMtkPorts();
    return ports.isEmpty() ? DetectedPort{} : ports.first();
}

DetectedPort PortDetector::getFirstSprdPort()
{
    auto ports = detectSprdPorts();
    return ports.isEmpty() ? DetectedPort{} : ports.first();
}

QStringList PortDetector::getAvailablePortNames()
{
    QStringList result;
#ifdef _WIN32
    auto ports = enumerateComPortsNative();
    for (const auto& p : ports)
        result.append(p.portName);
#else
    for (const auto& info : QSerialPortInfo::availablePorts())
        result.append(info.portName());
#endif
    return result;
}

void PortDetector::startWatching(PortType targetType, int intervalMs)
{
    m_watchTarget = targetType;
    m_watchInterval = intervalMs;
    m_watching = true;
    m_lastPorts = detectAllPorts();

    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PortDetector::onWatchTimer);
    timer->start(intervalMs);
}

void PortDetector::stopWatching()
{
    m_watching = false;
}

void PortDetector::onWatchTimer()
{
    if (!m_watching) return;

    auto currentPorts = detectAllPorts();

    // Check for new devices
    for (const auto& p : currentPorts) {
        bool found = false;
        for (const auto& old : m_lastPorts) {
            if (old.portName == p.portName) { found = true; break; }
        }
        if (!found && (m_watchTarget == PortType::Unknown || p.type == m_watchTarget)) {
            emit deviceDetected(p);
        }
    }

    // Check for removed devices
    for (const auto& old : m_lastPorts) {
        bool found = false;
        for (const auto& p : currentPorts) {
            if (p.portName == old.portName) { found = true; break; }
        }
        if (!found) {
            emit deviceRemoved(old.portName);
        }
    }

    m_lastPorts = currentPorts;
}

} // namespace sakura
