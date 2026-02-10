#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <functional>

namespace sakura {

enum class TransportType {
    None = 0,
    Serial,
    USB
};

// Abstract transport interface for device communication
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual qint64 write(const QByteArray& data) = 0;
    virtual QByteArray read(int maxSize, int timeoutMs = 5000) = 0;
    virtual QByteArray readExact(int size, int timeoutMs = 5000) = 0;

    virtual void flush() = 0;
    virtual void discardInput() = 0;
    virtual void discardOutput() = 0;

    virtual TransportType type() const = 0;
    virtual QString description() const = 0;

    // Progress callback for large transfers
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;
    void setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }

protected:
    void reportProgress(qint64 current, qint64 total) {
        if (m_progressCb) m_progressCb(current, total);
    }

    ProgressCallback m_progressCb;
};

} // namespace sakura
