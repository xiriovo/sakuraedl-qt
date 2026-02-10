#pragma once

#include "i_transport.h"
#include <QSerialPort>
#include <QMutex>
#include <memory>

namespace sakura {

class SerialTransport : public ITransport {
public:
    explicit SerialTransport(const QString& portName,
                              qint32 baudRate = 115200);
    ~SerialTransport() override;

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
    static bool isPortAvailable(const QString& portName);

private:
    QString m_portName;
    qint32 m_baudRate;
    std::unique_ptr<QSerialPort> m_port;
    QMutex m_mutex;
};

} // namespace sakura
