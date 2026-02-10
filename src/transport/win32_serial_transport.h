#pragma once

#include "i_transport.h"
#include <QMutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sakura {

/**
 * Win32 native serial transport using CreateFileA / ReadFile / WriteFile.
 *
 * Advantages over QSerialPort:
 * - Lower CPU usage (no Qt event loop overhead)
 * - More reliable for MTK BROM/Preloader short-lived devices
 * - Direct kernel32 I/O, no abstraction layer bugs
 * - Better timeout control via COMMTIMEOUTS
 */
class Win32SerialTransport : public ITransport {
public:
    explicit Win32SerialTransport(const QString& portName,
                                   qint32 baudRate = 115200);
    ~Win32SerialTransport() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;

    qint64 write(const QByteArray& data) override;
    QByteArray read(int maxSize, int timeoutMs = 5000) override;
    QByteArray readExact(int size, int timeoutMs = 5000) override;

    void flush() override;
    void discardInput() override;
    void discardOutput() override;

    TransportType type() const override { return TransportType::Serial; }
    QString description() const override;

    // Serial-specific
    void setBaudRate(qint32 rate);
    void setPortName(const QString& name);
    QString portName() const { return m_portName; }
    qint32 baudRate() const { return m_baudRate; }

    bool validateConnection();

    /**
     * Fast port availability check using CreateFileA.
     * Much faster than QSerialPortInfo::availablePorts().
     */
    static bool isPortAvailable(const QString& portName);

private:
    bool configurePort();
    bool setTimeouts(DWORD readIntervalTimeout,
                     DWORD readTotalTimeoutMultiplier,
                     DWORD readTotalTimeoutConstant,
                     DWORD writeTotalTimeoutMultiplier,
                     DWORD writeTotalTimeoutConstant);

    QString m_portName;
    qint32 m_baudRate;
#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
    QMutex m_mutex;
};

} // namespace sakura
