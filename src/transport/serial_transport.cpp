#include "serial_transport.h"
#include "core/logger.h"
#include <QSerialPortInfo>
#include <QElapsedTimer>

namespace sakura {

SerialTransport::SerialTransport(const QString& portName, qint32 baudRate)
    : m_portName(portName), m_baudRate(baudRate)
{
}

SerialTransport::~SerialTransport()
{
    close();
}

bool SerialTransport::open()
{
    QMutexLocker lock(&m_mutex);
    m_port = std::make_unique<QSerialPort>();
    m_port->setPortName(m_portName);
    m_port->setBaudRate(m_baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        LOG_ERROR(QString("Failed to open %1: %2").arg(m_portName, m_port->errorString()));
        m_port.reset();
        return false;
    }

    m_port->setReadBufferSize(1024 * 1024); // 1MB buffer
    LOG_INFO(QString("Opened %1 @ %2 baud").arg(m_portName).arg(m_baudRate));
    return true;
}

void SerialTransport::close()
{
    QMutexLocker lock(&m_mutex);
    if (m_port && m_port->isOpen()) {
        m_port->close();
        LOG_INFO("Closed " + m_portName);
    }
    m_port.reset();
}

bool SerialTransport::isOpen() const
{
    return m_port && m_port->isOpen();
}

qint64 SerialTransport::write(const QByteArray& data)
{
    QMutexLocker lock(&m_mutex);
    if (!m_port || !m_port->isOpen()) return -1;

    qint64 written = m_port->write(data);
    m_port->waitForBytesWritten(5000);
    return written;
}

QByteArray SerialTransport::read(int maxSize, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_port || !m_port->isOpen()) return {};

    if (m_port->bytesAvailable() == 0)
        m_port->waitForReadyRead(timeoutMs);

    return m_port->read(maxSize);
}

QByteArray SerialTransport::readExact(int size, int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_port || !m_port->isOpen()) return {};

    QByteArray result;
    result.reserve(size);
    QElapsedTimer timer;
    timer.start();

    while (result.size() < size) {
        if (timer.elapsed() > timeoutMs) {
            LOG_WARNING(QString("readExact timeout: got %1/%2 bytes in %3ms")
                            .arg(result.size()).arg(size).arg(timer.elapsed()));
            break;
        }
        if (m_port->bytesAvailable() == 0)
            m_port->waitForReadyRead(qMin(500, timeoutMs - static_cast<int>(timer.elapsed())));

        QByteArray chunk = m_port->read(size - result.size());
        if (!chunk.isEmpty())
            result.append(chunk);
    }
    return result;
}

void SerialTransport::flush()
{
    QMutexLocker lock(&m_mutex);
    if (m_port) m_port->flush();
}

void SerialTransport::discardInput()
{
    QMutexLocker lock(&m_mutex);
    if (m_port) {
        m_port->readAll();
        m_port->clear(QSerialPort::Input);
    }
}

void SerialTransport::discardOutput()
{
    QMutexLocker lock(&m_mutex);
    if (m_port) m_port->clear(QSerialPort::Output);
}

QString SerialTransport::description() const
{
    return QString("Serial[%1@%2]").arg(m_portName).arg(m_baudRate);
}

void SerialTransport::setBaudRate(qint32 rate)
{
    m_baudRate = rate;
    QMutexLocker lock(&m_mutex);
    if (m_port && m_port->isOpen())
        m_port->setBaudRate(rate);
}

void SerialTransport::setPortName(const QString& name)
{
    m_portName = name;
}

bool SerialTransport::validateConnection()
{
    QMutexLocker lock(&m_mutex);
    if (!m_port || !m_port->isOpen()) return false;
    // Attempt a minimal operation to verify port is alive
    return m_port->error() == QSerialPort::NoError;
}

bool SerialTransport::isPortAvailable(const QString& portName)
{
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        if (info.portName() == portName || info.systemLocation() == portName)
            return true;
    }
    return false;
}

} // namespace sakura
