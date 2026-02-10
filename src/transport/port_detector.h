#pragma once

#include <QObject>
#include <QStringList>
#include <QList>

namespace sakura {

enum class PortType {
    Unknown = 0,
    QualcommEdl9008,
    QualcommDload9006,
    QualcommDiag9091,
    MtkPreloader,
    MtkBrom,
    MtkDa,
    SpreadtrumDownload,
    Fastboot,
    Adb,
    Other
};

struct DetectedPort {
    QString portName;       // e.g. "COM3", "bus1-dev2"
    QString description;    // Device description string
    QString friendlyName;   // Device Manager friendly name
    QString deviceId;       // "VID:PID" formatted string
    QString instanceId;     // Device instance ID (Win32)
    QString deviceClass;    // Device Manager class (Ports, USB, Modem, etc.)
    QString driver;         // Driver name
    PortType type = PortType::Unknown;
    uint16_t vid = 0;
    uint16_t pid = 0;
    bool isEdl = false;
    bool isMtk = false;
    bool isSprd = false;
    bool isFastboot = false;
    bool isUsb = false;     // true = USB (libusb), false = Serial (COM)
    bool hasComPort = false; // true = has COM port assigned
    uint32_t devStatus = 0; // Device Manager status (CM_Get_DevNode_Status)
    uint32_t devProblem = 0; // Device Manager problem code
};

class PortDetector : public QObject {
    Q_OBJECT

public:
    explicit PortDetector(QObject* parent = nullptr);

    static QList<DetectedPort> detectAllPorts();
    static QList<DetectedPort> detectEdlPorts();
    static QList<DetectedPort> detectMtkPorts();
    static QList<DetectedPort> detectSprdPorts();
    static QList<DetectedPort> detectFastbootDevices();

    static DetectedPort getFirstEdlPort();
    static DetectedPort getFirstMtkPort();
    static DetectedPort getFirstSprdPort();

    static PortType identifyPortType(uint16_t vid, uint16_t pid);
    static QStringList getAvailablePortNames();

    /**
     * Win32 native COM port enumeration using SetupDi API.
     * Much faster and more reliable than QSerialPortInfo, especially
     * for MTK BROM/Preloader devices that appear briefly.
     *
     * Returns all COM ports with VID/PID extracted from device instance ID.
     */
    static QList<DetectedPort> enumerateComPortsNative();

    /**
     * Full Device Manager scan across multiple device classes.
     * Scans: Ports, USB, USBDevice, Modem, and unknown/other classes.
     *
     * This catches devices that:
     * - Haven't loaded a VCOM driver yet (raw USB)
     * - Are in "Unknown Device" state
     * - Are under USB class without COM port
     *
     * Filters by known VIDs (MTK, Qualcomm, Spreadtrum, etc.)
     */
    static QList<DetectedPort> enumerateDeviceManager();

    // Wait for specific device type to appear
    void startWatching(PortType targetType, int intervalMs = 500);
    void stopWatching();

signals:
    void deviceDetected(const DetectedPort& port);
    void deviceRemoved(const QString& portName);

private slots:
    void onWatchTimer();

private:
    QList<DetectedPort> m_lastPorts;
    PortType m_watchTarget = PortType::Unknown;
    int m_watchInterval = 500;
    bool m_watching = false;
};

} // namespace sakura
