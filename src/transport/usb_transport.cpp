#include "usb_transport.h"
#include "core/logger.h"
#include <QElapsedTimer>

// libusb header - adjust path based on your installation
#include <libusb-1.0/libusb.h>

namespace sakura {

libusb_context* UsbTransport::s_context = nullptr;
int UsbTransport::s_refCount = 0;

UsbTransport::UsbTransport()
{
    initLibusb();
}

UsbTransport::UsbTransport(uint16_t vid, uint16_t pid)
    : m_vid(vid), m_pid(pid)
{
    initLibusb();
}

UsbTransport::~UsbTransport()
{
    close();
    exitLibusb();
}

bool UsbTransport::initLibusb()
{
    if (s_refCount++ == 0) {
        int ret = libusb_init(&s_context);
        if (ret != 0) {
            LOG_ERROR(QString("libusb_init failed: %1").arg(libusb_strerror(static_cast<libusb_error>(ret))));
            s_refCount--;
            return false;
        }
    }
    return true;
}

void UsbTransport::exitLibusb()
{
    if (--s_refCount <= 0 && s_context) {
        libusb_exit(s_context);
        s_context = nullptr;
        s_refCount = 0;
    }
}

bool UsbTransport::open()
{
    return openByVidPid(m_vid, m_pid);
}

bool UsbTransport::openByVidPid(uint16_t vid, uint16_t pid)
{
    QMutexLocker lock(&m_mutex);
    if (!s_context) return false;

    m_vid = vid;
    m_pid = pid;

    m_handle = libusb_open_device_with_vid_pid(s_context, vid, pid);
    if (!m_handle) {
        LOG_ERROR(QString("No USB device found with VID=%1 PID=%2")
                      .arg(vid, 4, 16, QChar('0')).arg(pid, 4, 16, QChar('0')));
        return false;
    }

    if (!claimInterface())
        return false;

    if (!findEndpoints())
        return false;

    LOG_INFO(QString("USB device opened: VID=%1 PID=%2")
                 .arg(vid, 4, 16, QChar('0')).arg(pid, 4, 16, QChar('0')));
    return true;
}

void UsbTransport::close()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle) {
        // Reset the device before closing to prevent the device from
        // being locked in a claimed state. Without this, some devices
        // (especially MTK) may require a system restart to reconnect.
        int ret = libusb_reset_device(m_handle);
        if (ret != 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
            // NOT_FOUND means device was already disconnected, which is fine
            LOG_WARNING(QString("libusb_reset_device: %1 (non-fatal)")
                            .arg(libusb_strerror(static_cast<libusb_error>(ret))));
        }

        libusb_release_interface(m_handle, m_interface);
        libusb_close(m_handle);
        m_handle = nullptr;
        LOG_INFO("USB device closed (with reset)");
    }
}

bool UsbTransport::isOpen() const
{
    return m_handle != nullptr;
}

qint64 UsbTransport::write(const QByteArray& data)
{
    QMutexLocker lock(&m_mutex);
    if (!m_handle) return -1;

    int transferred = 0;
    int ret = libusb_bulk_transfer(m_handle, m_epOut,
                                    const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data.data())),
                                    data.size(), &transferred, 5000);
    if (ret != 0) {
        LOG_ERROR(QString("USB write error: %1").arg(libusb_strerror(static_cast<libusb_error>(ret))));
        return -1;
    }
    return transferred;
}

QByteArray UsbTransport::read(int maxSize, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_handle) return {};

    QByteArray buffer(maxSize, 0);
    int transferred = 0;
    int ret = libusb_bulk_transfer(m_handle, m_epIn,
                                    reinterpret_cast<unsigned char*>(buffer.data()),
                                    maxSize, &transferred, timeoutMs);
    if (ret != 0 && ret != LIBUSB_ERROR_TIMEOUT) {
        LOG_ERROR(QString("USB read error: %1").arg(libusb_strerror(static_cast<libusb_error>(ret))));
        return {};
    }
    buffer.resize(transferred);
    return buffer;
}

QByteArray UsbTransport::readExact(int size, int timeoutMs)
{
    QByteArray result;
    result.reserve(size);
    QElapsedTimer timer;
    timer.start();

    while (result.size() < size) {
        if (timer.elapsed() > timeoutMs) break;
        int remaining = size - result.size();
        QByteArray chunk = read(remaining, qMin(1000, timeoutMs - static_cast<int>(timer.elapsed())));
        if (!chunk.isEmpty())
            result.append(chunk);
        else
            break;
    }
    return result;
}

void UsbTransport::flush() {}

void UsbTransport::discardInput()
{
    // Read and discard any pending data
    QByteArray dummy = read(65536, 100);
    Q_UNUSED(dummy);
}

void UsbTransport::discardOutput() {}

QString UsbTransport::description() const
{
    return QString("USB[%1:%2]").arg(m_vid, 4, 16, QChar('0')).arg(m_pid, 4, 16, QChar('0'));
}

void UsbTransport::setEndpoints(uint8_t epIn, uint8_t epOut)
{
    m_epIn = epIn;
    m_epOut = epOut;
}

bool UsbTransport::claimInterface()
{
    // Detach kernel driver if active
    if (libusb_kernel_driver_active(m_handle, m_interface) == 1) {
        libusb_detach_kernel_driver(m_handle, m_interface);
    }

    int ret = libusb_claim_interface(m_handle, m_interface);
    if (ret != 0) {
        LOG_ERROR(QString("Failed to claim interface %1: %2")
                      .arg(m_interface).arg(libusb_strerror(static_cast<libusb_error>(ret))));
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }
    return true;
}

bool UsbTransport::findEndpoints()
{
    libusb_device* dev = libusb_get_device(m_handle);
    struct libusb_config_descriptor* config;
    int ret = libusb_get_active_config_descriptor(dev, &config);
    if (ret != 0) return false;

    bool foundIn = false, foundOut = false;

    for (int i = 0; i < config->bNumInterfaces && !(foundIn && foundOut); i++) {
        const struct libusb_interface& iface = config->interface[i];
        for (int j = 0; j < iface.num_altsetting; j++) {
            const struct libusb_interface_descriptor& alt = iface.altsetting[j];
            for (int k = 0; k < alt.bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor& ep = alt.endpoint[k];
                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        m_epIn = ep.bEndpointAddress;
                        foundIn = true;
                    } else {
                        m_epOut = ep.bEndpointAddress;
                        foundOut = true;
                    }
                }
            }
        }
    }

    libusb_free_config_descriptor(config);
    return foundIn && foundOut;
}

QList<UsbDeviceInfo> UsbTransport::enumerateDevices(uint16_t vid, uint16_t pid)
{
    QList<UsbDeviceInfo> result;
    if (!s_context) {
        // Try to init if not already done
        if (!initLibusb()) return result;
    }

    libusb_device** devList;
    ssize_t count = libusb_get_device_list(s_context, &devList);
    if (count < 0) return result;

    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devList[i], &desc) != 0)
            continue;

        if (vid != 0 && desc.idVendor != vid) continue;
        if (pid != 0 && desc.idProduct != pid) continue;

        UsbDeviceInfo info;
        info.vid = desc.idVendor;
        info.pid = desc.idProduct;
        info.path = QString("bus%1-dev%2")
                        .arg(libusb_get_bus_number(devList[i]))
                        .arg(libusb_get_device_address(devList[i]));

        // IMPORTANT: Only open device to read descriptors if it's a device
        // we actually care about. Opening USB devices can interfere with
        // other drivers (especially MTK VCOM) and may cause device locking.
        // Only read string descriptors for devices with known VIDs.
        bool shouldReadStrings = (desc.idVendor == MTK_VID ||
                                   desc.idVendor == QUALCOMM_VID ||
                                   desc.idVendor == SPRD_VID ||
                                   desc.idVendor == GOOGLE_VID);

        if (shouldReadStrings) {
            libusb_device_handle* h = nullptr;
            if (libusb_open(devList[i], &h) == 0 && h) {
                unsigned char buf[256];
                if (desc.iSerialNumber > 0) {
                    int len = libusb_get_string_descriptor_ascii(h, desc.iSerialNumber, buf, sizeof(buf));
                    if (len > 0) info.serial = QString::fromLatin1(reinterpret_cast<char*>(buf), len);
                }
                if (desc.iProduct > 0) {
                    int len = libusb_get_string_descriptor_ascii(h, desc.iProduct, buf, sizeof(buf));
                    if (len > 0) info.description = QString::fromLatin1(reinterpret_cast<char*>(buf), len);
                }
                libusb_close(h);
            }
        }

        result.append(info);
    }

    libusb_free_device_list(devList, 1);
    return result;
}

} // namespace sakura
