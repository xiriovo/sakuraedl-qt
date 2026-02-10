#pragma once

#include "i_transport.h"
#include <QMutex>
#include <cstdint>

struct libusb_context;
struct libusb_device_handle;

namespace sakura {

struct UsbDeviceInfo {
    uint16_t vid = 0;
    uint16_t pid = 0;
    QString serial;
    QString description;
    QString path;
};

class UsbTransport : public ITransport {
public:
    UsbTransport();
    explicit UsbTransport(uint16_t vid, uint16_t pid);
    ~UsbTransport() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;

    qint64 write(const QByteArray& data) override;
    QByteArray read(int maxSize, int timeoutMs = 5000) override;
    QByteArray readExact(int size, int timeoutMs = 5000) override;

    void flush() override;
    void discardInput() override;
    void discardOutput() override;

    TransportType type() const override { return TransportType::USB; }
    QString description() const override;

    // USB-specific
    bool openByVidPid(uint16_t vid, uint16_t pid);
    void setEndpoints(uint8_t epIn, uint8_t epOut);

    static QList<UsbDeviceInfo> enumerateDevices(uint16_t vid = 0, uint16_t pid = 0);
    static bool initLibusb();
    static void exitLibusb();

    // Known VID/PIDs
    static constexpr uint16_t QUALCOMM_VID = 0x05C6;
    static constexpr uint16_t QUALCOMM_EDL_PID = 0x9008;
    static constexpr uint16_t QUALCOMM_DIAG_PID = 0x9091;
    static constexpr uint16_t MTK_VID = 0x0E8D;
    static constexpr uint16_t MTK_BROM_PID = 0x0003;       // BROM (Boot ROM)
    static constexpr uint16_t MTK_BROM_LEGACY_PID = 0x0002; // BROM Legacy
    static constexpr uint16_t MTK_PRELOADER_PID = 0x2000;   // Preloader
    static constexpr uint16_t MTK_PRELOADER_V2_PID = 0x0616;// Preloader V2
    static constexpr uint16_t MTK_DA_PID = 0x2001;          // DA mode
    static constexpr uint16_t MTK_DA_CDC_PID = 0x2003;      // DA CDC
    static constexpr uint16_t MTK_DA_V2_PID = 0x2004;       // DA V2
    static constexpr uint16_t MTK_DA_V3_PID = 0x2005;       // DA V3
    static constexpr uint16_t SPRD_VID = 0x1782;
    static constexpr uint16_t SPRD_PID = 0x4D00;
    static constexpr uint16_t GOOGLE_VID = 0x18D1;
    static constexpr uint16_t FASTBOOT_PID = 0xD00D;

private:
    bool claimInterface();
    bool findEndpoints();

    uint16_t m_vid = 0;
    uint16_t m_pid = 0;
    uint8_t m_epIn = 0x81;
    uint8_t m_epOut = 0x01;
    int m_interface = 0;

    libusb_device_handle* m_handle = nullptr;
    static libusb_context* s_context;
    static int s_refCount;
    QMutex m_mutex;
};

} // namespace sakura
