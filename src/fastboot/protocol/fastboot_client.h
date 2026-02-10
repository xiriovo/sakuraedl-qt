#pragma once

#include "fastboot_protocol.h"
#include "transport/usb_transport.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>

namespace sakura {

// ---------------------------------------------------------------------------
// FastbootClient – speaks the Fastboot USB protocol over a UsbTransport
// ---------------------------------------------------------------------------

class FastbootClient : public QObject {
    Q_OBJECT

public:
    using ProgressCallback = std::function<void(qint64 current, qint64 total)>;

    /// Construct a client that communicates over the given transport.
    /// The transport must already be opened and is NOT owned by this class.
    explicit FastbootClient(UsbTransport* transport, QObject* parent = nullptr);
    ~FastbootClient() override = default;

    // --- Connection --------------------------------------------------------

    /// Verify that the device speaks Fastboot (sends "getvar:version").
    bool connect();

    /// Return true when connected and transport is open.
    bool isConnected() const { return m_connected; }

    // --- Core Fastboot commands --------------------------------------------

    /// Retrieve a bootloader variable.  Returns empty string on failure.
    QString getVariable(const QString& name);

    /// Download raw data to the device RAM (download + payload).
    bool download(const QByteArray& data);

    /// Flash a partition with the supplied image data.
    /// Handles download + "flash:<partition>".
    bool flash(const QString& partition, const QByteArray& data);

    /// Erase a partition.
    bool erase(const QString& partition);

    // --- Reboot commands ---------------------------------------------------

    bool reboot();
    bool rebootBootloader();
    bool rebootRecovery();
    bool rebootFastbootd();

    // --- OEM commands ------------------------------------------------------

    bool oemUnlock();
    bool oemLock();
    bool oemCommand(const QString& command);

    // --- Low-level ---------------------------------------------------------

    /// Send a command string and wait for a final OKAY / FAIL response.
    /// Intermediate INFO messages are emitted via infoReceived().
    FastbootResponse sendCommand(const QString& command);

    /// Send raw data after a DATA response, with progress reporting.
    bool sendData(const QByteArray& data);

    // --- Configuration -----------------------------------------------------

    void setProgressCallback(ProgressCallback cb) { m_progressCb = std::move(cb); }
    void setResponseTimeoutMs(int ms) { m_responseTimeoutMs = ms; }
    int  responseTimeoutMs() const    { return m_responseTimeoutMs; }

    /// Maximum download size the device advertises (queried during connect).
    uint32_t maxDownloadSize() const { return m_maxDownloadSize; }

signals:
    /// Emitted for every INFO response received during a command.
    void infoReceived(const QString& message);

    /// Transfer progress.
    void progressUpdated(qint64 current, qint64 total);

private:
    /// Read a single response packet from the device.
    FastbootResponse readResponse();

    /// Read responses until a final (non-INFO) response arrives.
    FastbootResponse readFinalResponse();

    void reportProgress(qint64 current, qint64 total);

    UsbTransport*    m_transport        = nullptr;
    bool             m_connected        = false;
    uint32_t         m_maxDownloadSize  = FastbootProtocol::MAX_DOWNLOAD_SIZE_DEFAULT;
    int              m_responseTimeoutMs = 30000; // 30 s default
    ProgressCallback m_progressCb;
};

} // namespace sakura
