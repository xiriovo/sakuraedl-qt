#include "win32_serial_transport.h"
#include "core/logger.h"
#include <QElapsedTimer>
#include <QThread>

#ifdef _WIN32

namespace sakura {

static const char* LOG_TAG = "Win32Serial";

Win32SerialTransport::Win32SerialTransport(const QString& portName, qint32 baudRate)
    : m_portName(portName), m_baudRate(baudRate)
{
}

Win32SerialTransport::~Win32SerialTransport()
{
    close();
}

bool Win32SerialTransport::open()
{
    QMutexLocker lock(&m_mutex);

    if (m_handle != INVALID_HANDLE_VALUE) {
        // Already open
        return true;
    }

    // Build the device path: \\.\COMx
    QString devicePath = m_portName;
    if (!devicePath.startsWith("\\\\.\\")) {
        devicePath = "\\\\.\\" + devicePath;
    }

    QByteArray pathBytes = devicePath.toLatin1();

    m_handle = CreateFileA(
        pathBytes.constData(),
        GENERIC_READ | GENERIC_WRITE,
        0,                          // No sharing
        nullptr,                    // No security attributes
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,      // Synchronous I/O (no OVERLAPPED)
        nullptr                     // No template
    );

    if (m_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LOG_ERROR_CAT(LOG_TAG, QString("CreateFileA failed for %1: error %2")
                                   .arg(m_portName).arg(err));
        return false;
    }

    if (!configurePort()) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set default timeouts: non-blocking initial read
    // ReadIntervalTimeout=MAXDWORD, Multiplier=0, Constant=0 → return immediately
    // The actual read timeouts are set per-call in read()/readExact()
    if (!setTimeouts(MAXDWORD, 0, 0, 0, 5000)) {
        LOG_WARNING_CAT(LOG_TAG, "Failed to set initial timeouts");
    }

    // NOTE: Do NOT PurgeComm here!
    // Some devices (e.g. Qualcomm EDL) send data immediately when the port
    // is created. Purging here would discard the Sahara Hello packet.
    // Callers should explicitly call discardInput() if they need to clear stale data.

    LOG_INFO_CAT(LOG_TAG, QString("Opened %1 @ %2 baud (Win32 native)")
                              .arg(m_portName).arg(m_baudRate));
    return true;
}

void Win32SerialTransport::close()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        // Flush pending writes
        FlushFileBuffers(m_handle);
        // Purge buffers
        PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        LOG_INFO_CAT(LOG_TAG, "Closed " + m_portName);
    }
}

bool Win32SerialTransport::isOpen() const
{
    return m_handle != INVALID_HANDLE_VALUE;
}

qint64 Win32SerialTransport::write(const QByteArray& data)
{
    QMutexLocker lock(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) return -1;

    const char* ptr = data.constData();
    int remaining = data.size();
    int totalWritten = 0;

    while (remaining > 0) {
        DWORD written = 0;
        BOOL ok = WriteFile(m_handle, ptr, static_cast<DWORD>(remaining), &written, nullptr);
        if (!ok) {
            DWORD err = GetLastError();
            LOG_ERROR_CAT(LOG_TAG, QString("WriteFile error: %1").arg(err));
            return totalWritten > 0 ? totalWritten : -1;
        }
        ptr += written;
        remaining -= static_cast<int>(written);
        totalWritten += static_cast<int>(written);
    }

    return totalWritten;
}

QByteArray Win32SerialTransport::read(int maxSize, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) return {};

    // Set timeouts for this read operation
    // MAXDWORD,0,timeoutMs = return immediately with available data,
    // but wait up to timeoutMs if no data at all
    COMMTIMEOUTS ct;
    ct.ReadIntervalTimeout = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = MAXDWORD;
    ct.ReadTotalTimeoutConstant = static_cast<DWORD>(timeoutMs);
    ct.WriteTotalTimeoutMultiplier = 0;
    ct.WriteTotalTimeoutConstant = 5000;
    SetCommTimeouts(m_handle, &ct);

    QByteArray buffer(maxSize, 0);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(m_handle, buffer.data(), static_cast<DWORD>(maxSize), &bytesRead, nullptr);

    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR_CAT(LOG_TAG, QString("ReadFile error: %1").arg(err));
        return {};
    }

    buffer.resize(static_cast<int>(bytesRead));
    return buffer;
}

QByteArray Win32SerialTransport::readExact(int size, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) return {};

    QByteArray result;
    result.reserve(size);
    QElapsedTimer timer;
    timer.start();

    while (result.size() < size) {
        int elapsed = static_cast<int>(timer.elapsed());
        if (elapsed >= timeoutMs) {
            LOG_WARNING_CAT(LOG_TAG, QString("readExact timeout: got %1/%2 bytes in %3ms")
                                         .arg(result.size()).arg(size).arg(elapsed));
            break;
        }

        int remaining = size - result.size();
        int remainingTime = timeoutMs - elapsed;

        // Timeout strategy:
        // - First read (no data yet): wait up to full remainingTime for first byte
        // - Subsequent reads (have some data): use inter-byte timeout of 100ms
        COMMTIMEOUTS ct;
        if (result.isEmpty()) {
            // Waiting for first byte — use full timeout as total constant
            ct.ReadIntervalTimeout = 100;
            ct.ReadTotalTimeoutMultiplier = 0;
            ct.ReadTotalTimeoutConstant = static_cast<DWORD>(remainingTime);
        } else {
            // Already have some data — use shorter timeout per chunk
            ct.ReadIntervalTimeout = 100;  // 100ms between bytes max
            ct.ReadTotalTimeoutMultiplier = 0;
            ct.ReadTotalTimeoutConstant = static_cast<DWORD>(qMin(remainingTime, 2000));
        }
        ct.WriteTotalTimeoutMultiplier = 0;
        ct.WriteTotalTimeoutConstant = 5000;
        SetCommTimeouts(m_handle, &ct);

        QByteArray chunk(remaining, 0);
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_handle, chunk.data(), static_cast<DWORD>(remaining), &bytesRead, nullptr);

        if (!ok) {
            DWORD err = GetLastError();
            LOG_ERROR_CAT(LOG_TAG, QString("ReadFile error in readExact: %1").arg(err));
            break;
        }

        if (bytesRead > 0) {
            chunk.resize(static_cast<int>(bytesRead));
            result.append(chunk);
        }
        // If bytesRead == 0 and timeout reached, the outer loop will check elapsed time
    }

    return result;
}

void Win32SerialTransport::flush()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(m_handle);
    }
}

void Win32SerialTransport::discardInput()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_RXABORT);
    }
}

void Win32SerialTransport::discardOutput()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        PurgeComm(m_handle, PURGE_TXCLEAR | PURGE_TXABORT);
    }
}

QString Win32SerialTransport::description() const
{
    return QString("Win32Serial[%1@%2]").arg(m_portName).arg(m_baudRate);
}

void Win32SerialTransport::setBaudRate(qint32 rate)
{
    m_baudRate = rate;
    QMutexLocker lock(&m_mutex);
    if (m_handle != INVALID_HANDLE_VALUE) {
        configurePort();  // Reconfigure with new baud rate
    }
}

void Win32SerialTransport::setPortName(const QString& name)
{
    m_portName = name;
}

bool Win32SerialTransport::validateConnection()
{
    QMutexLocker lock(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    // Check if the handle is still valid by querying comm state
    DCB dcb;
    dcb.DCBlength = sizeof(DCB);
    return GetCommState(m_handle, &dcb) != 0;
}

bool Win32SerialTransport::isPortAvailable(const QString& portName)
{
    QString devicePath = portName;
    if (!devicePath.startsWith("\\\\.\\")) {
        devicePath = "\\\\.\\" + devicePath;
    }

    QByteArray pathBytes = devicePath.toLatin1();
    HANDLE h = CreateFileA(
        pathBytes.constData(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );

    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return true;
    }
    return false;
}

bool Win32SerialTransport::configurePort()
{
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    // Set buffer sizes: 1MB input, 1MB output
    SetupComm(m_handle, 1024 * 1024, 1024 * 1024);

    DCB dcb;
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        LOG_ERROR_CAT(LOG_TAG, QString("GetCommState failed: %1").arg(GetLastError()));
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(m_baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(m_handle, &dcb)) {
        LOG_ERROR_CAT(LOG_TAG, QString("SetCommState failed: %1").arg(GetLastError()));
        return false;
    }

    return true;
}

bool Win32SerialTransport::setTimeouts(DWORD readIntervalTimeout,
                                        DWORD readTotalTimeoutMultiplier,
                                        DWORD readTotalTimeoutConstant,
                                        DWORD writeTotalTimeoutMultiplier,
                                        DWORD writeTotalTimeoutConstant)
{
    if (m_handle == INVALID_HANDLE_VALUE) return false;

    COMMTIMEOUTS ct;
    ct.ReadIntervalTimeout = readIntervalTimeout;
    ct.ReadTotalTimeoutMultiplier = readTotalTimeoutMultiplier;
    ct.ReadTotalTimeoutConstant = readTotalTimeoutConstant;
    ct.WriteTotalTimeoutMultiplier = writeTotalTimeoutMultiplier;
    ct.WriteTotalTimeoutConstant = writeTotalTimeoutConstant;

    if (!SetCommTimeouts(m_handle, &ct)) {
        LOG_ERROR_CAT(LOG_TAG, QString("SetCommTimeouts failed: %1").arg(GetLastError()));
        return false;
    }
    return true;
}

} // namespace sakura

#endif // _WIN32
